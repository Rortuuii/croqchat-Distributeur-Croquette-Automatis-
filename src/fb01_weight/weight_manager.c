/* Lecture du HX711, filtrage du poids, tare et calibration. */
#include "weight_manager.h"
#include "croqchat_config.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "motor_controller.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#define NOMBRE_MESURES 20
#define TAILLE_FILTRE 5

/* Format conserve pour reutiliser la calibration deja enregistree. */
static struct {
    int32_t zero;
    float coefficient; /* Comptes HX711 par gramme. */
} reglages;

/* Instantané partagé. Le brut reste disponible indépendamment de la conversion en grammes. */
static weight_snapshot_t poids;
static portMUX_TYPE verrou = portMUX_INITIALIZER_UNLOCKED;
static enum { REPOS, TARE, CALIBRATION } operation;
static int64_t somme;
static unsigned compteur;
static float reference;
static int32_t filtre[TAILLE_FILTRE];
static unsigned position_filtre;
static unsigned nombre_filtre;
static uint32_t nombre_tares;
static int64_t fin_operation_us;
static int64_t derniere_mesure_us;
static int64_t prochaine_relance_us;
#define AGE_MESURE_MAX_US INT64_C(1000000)
#define DELAI_RELANCE_US INT64_C(2000000)

/* Moyenne des cinq dernieres lectures, ou des lectures disponibles au debut. */
static float filtrer(int32_t brut)
{
    filtre[position_filtre] = brut;
    position_filtre = (position_filtre + 1) % TAILLE_FILTRE;
    if (nombre_filtre < TAILLE_FILTRE) nombre_filtre++;

    int64_t total = 0;
    for (unsigned i = 0; i < nombre_filtre; i++) total += filtre[i];
    return (float)total / nombre_filtre;
}

/* Enregistre les reglages dans la memoire permanente. */
static bool sauvegarder(void)
{
    nvs_handle_t memoire;
    esp_err_t e = nvs_open("croq_poids", NVS_READWRITE, &memoire);
    if (e == ESP_OK) {
        e = nvs_set_blob(memoire, "reglages", &reglages, sizeof(reglages));
        if (e == ESP_OK) e = nvs_commit(memoire);
        nvs_close(memoire);
    }
    if (e != ESP_OK) ESP_LOGE("Balance", "Sauvegarde impossible : %s", esp_err_to_name(e));
    return e == ESP_OK;
}

/* Configure le HX711 et recharge les reglages sauvegardes. */
void weight_manager_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CROQCHAT_GPIO_HX711_SCK,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&config);
    gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 0);
    config.pin_bit_mask = 1ULL << CROQCHAT_GPIO_HX711_DOUT;
    config.mode = GPIO_MODE_INPUT;
    gpio_config(&config);

    poids = (weight_snapshot_t){0};
    reglages.zero = 0;
    reglages.coefficient = 0;
    operation = REPOS;
    position_filtre = 0;
    nombre_filtre = 0;
    nombre_tares = 0;

    derniere_mesure_us = 0;
    prochaine_relance_us = esp_timer_get_time() + DELAI_RELANCE_US;
    nvs_handle_t memoire;
    size_t taille = sizeof(reglages);
    esp_err_t e = nvs_open("croq_poids", NVS_READWRITE, &memoire);
    if (e == ESP_OK) {
        e = nvs_get_blob(memoire, "reglages", &reglages, &taille);
        nvs_close(memoire);
    }
    if (e != ESP_OK || taille != sizeof(reglages) || !isfinite(reglages.coefficient)
        || fabsf(reglages.coefficient) < 0.001f) {
        memset(&reglages, 0, sizeof(reglages));
    }
    poids.calibrated = reglages.coefficient != 0;
    poids.tared = poids.calibrated;
}

/* 24 bits signes + une impulsion : canal A, gain 128.
 * La section critique garantit les impulsions courtes du HX711. */
static int32_t lire_brut(void)
{
    uint32_t brut = 0;
    portENTER_CRITICAL(&verrou);
    for (int bit = 0; bit < 24; bit++) {
        gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 1);
        esp_rom_delay_us(1);
        brut = (brut << 1) | gpio_get_level(CROQCHAT_GPIO_HX711_DOUT);
        gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 0);
        esp_rom_delay_us(1);
    }
    gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 0);
    portEXIT_CRITICAL(&verrou);

    if (brut & 0x800000U) brut |= 0xFF000000U;
    return (int32_t)brut;
}

/* Indique si une tare ou une calibration est en cours. */
bool weight_manager_is_busy(void)
{
    return operation != REPOS;
}
/* Compte les tares reussies pour reconnaitre une nouvelle tare. */
uint32_t weight_manager_tare_count(void) { return nombre_tares; }
/* Annule la tare ou la calibration en cours. */
void weight_manager_cancel_operation(void)
{
    if (operation != REPOS) poids.operation_result = 5;
    operation = REPOS;
}

/* Renvoie une copie du poids et de l etat de la balance. */
weight_snapshot_t weight_manager_get_snapshot(void)
{
    weight_snapshot_t lecture = poids;
    int64_t age = esp_timer_get_time() - derniere_mesure_us;
    if (age < 0 || age >= AGE_MESURE_MAX_US) lecture.sensor_ready = false;
    return lecture;
}

/* Verifie les conditions puis prepare les mesures de tare ou de calibration. */
/* Valide les préconditions, puis prépare la collecte des échantillons dans la boucle principale. */
static bool demarrer(bool calibration, float masse)
{
    if (weight_manager_is_busy() || motor_controller_is_running()) return false;
    if (calibration && (!poids.tared || !isfinite(masse) || masse <= 0
        || masse > CROQCHAT_MAX_MASS_GRAMS)) return false;
    poids.operation_result = 1;
    operation = calibration ? CALIBRATION : TARE;
    reference = masse;
    somme = 0;
    compteur = 0;
    fin_operation_us = esp_timer_get_time() + INT64_C(10000000);
    return true;
}

/* Demande de prendre le poids actuel comme nouveau zero. */
bool weight_manager_demander_tare(void)
{
    return demarrer(false, 0);
}

/* Demande une calibration avec une masse connue en grammes. */
bool weight_manager_demander_calibration(float masse_reference)
{
    return demarrer(true, masse_reference);
}

/* Traite les mesures et fait avancer la tare ou la calibration. */
/* Lit les conversions disponibles et fait avancer la tare ou la calibration sans attendre le prochain échantillon. */
void weight_manager_update(void)
{
    if (weight_manager_is_busy() && esp_timer_get_time() >= fin_operation_us) {
        operation = REPOS;
        poids.operation_result = 2;
        ESP_LOGE("Balance", "Tare/calibration interrompue : delai de 10 secondes depasse");
    }
    if (gpio_get_level(CROQCHAT_GPIO_HX711_DOUT) != 0) {
        int64_t maintenant = esp_timer_get_time();
        if (maintenant >= prochaine_relance_us) {
            /* PD_SCK haut plus de 60 us met le HX711 en veille.
             * Le retour a zero redemarre les conversions sur le canal A. */
            gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 1);
            esp_rom_delay_us(80);
            gpio_set_level(CROQCHAT_GPIO_HX711_SCK, 0);
            prochaine_relance_us = maintenant + DELAI_RELANCE_US;
            position_filtre = 0;
            nombre_filtre = 0;
            compteur = 0;
            somme = 0;
            ESP_LOGW("Balance", "HX711 : relance apres absence de conversion");
        }
        return;
    }

    int32_t brut = lire_brut();
    poids.raw = brut;
    poids.sensor_ready = true;
    derniere_mesure_us = esp_timer_get_time();
    prochaine_relance_us = derniere_mesure_us + DELAI_RELANCE_US;

    if (weight_manager_is_busy()) {
        somme += brut;
        if (++compteur < NOMBRE_MESURES) return;
        brut = (int32_t)(somme / NOMBRE_MESURES);

        if (operation == TARE) {
            reglages.zero = brut;
            poids.tared = true;
            ++nombre_tares;
        } else {
            float ecart = (float)brut - reglages.zero;
            float coefficient = ecart / reference;
            if (!isfinite(coefficient) || fabsf(ecart) < 20.0f || fabsf(coefficient) < 0.001f) {
                operation = REPOS;
                poids.operation_result = 3;
                ESP_LOGE("Balance", "Calibration refusee : masse de reference non detectee");
                return;
            }
            reglages.coefficient = coefficient;
            poids.calibrated = true;
        }
        /* Le zero de chaque portion reste en RAM ; seule la calibration est persistante. */
        poids.operation_result = operation == CALIBRATION && !sauvegarder() ? 4 : 0;
        operation = REPOS;
        position_filtre = 0;
        nombre_filtre = 0;
    }

    float brut_moyen = filtrer(brut);
    poids.grams = poids.calibrated ? (brut_moyen - reglages.zero) / reglages.coefficient : 0;
}
