/* Coordination des modules et des modes choisis dans main.c. */
#include "app_controller.h"
#include "storage.h"
#include "weight_manager.h"
#include "dispenser_controller.h"
#include "safety_manager.h"
#include "meal_scheduler.h"
#include "ble_manager.h"
#include "cat_identification.h"
#include "manual_badge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TickType_t dernier_affichage;
static app_options_t fonctions;

/* Demarre les modules demandes et tient compte de leurs dependances. */
void app_controller_init(const app_options_t *options)
{
    fonctions = options ? *options : (app_options_t){0};
    if (!fonctions.activer_balance) {
        fonctions.activer_scheduler = false;
        fonctions.activer_badge_manuel = false; /* Toute distribution doit pouvoir faire sa tare. */
    }
    if (!fonctions.activer_rfid) fonctions.activer_badge_manuel = false;
    ESP_ERROR_CHECK(storage_init());
    if (fonctions.activer_balance) {
        weight_manager_init();
    }
    dispenser_controller_init();
    ESP_ERROR_CHECK(safety_manager_init());
    if (fonctions.activer_scheduler) {
        esp_err_t erreur = meal_scheduler_init();
        if (erreur != ESP_OK) {
            ESP_LOGE("SCHEDULER", "Planning indisponible : %s", esp_err_to_name(erreur));
        }
    }
    manual_badge_init(fonctions.activer_badge_manuel);
    /* Une panne RFID laisse le mode badge inactif ; les repas et le BLE restent disponibles. */
    if (fonctions.activer_rfid) {
        esp_err_t erreur = cat_identification_init();
        if (erreur != ESP_OK) {
            ESP_LOGW("RFID", "Lecteur indisponible : %s ; badge manuel inactif", esp_err_to_name(erreur));
        }
    }
    ble_manager_set_features(fonctions.activer_balance, fonctions.activer_scheduler);
    if (fonctions.activer_ble) {
        ESP_ERROR_CHECK(ble_manager_start());
    }
    dernier_affichage = xTaskGetTickCount();
}

/* Examine les repas echus et lance ceux autorises par la securite. */
static void traiter_repas(bool arret)
{
    if (!dispenser_controller_is_busy())
        meal_scheduler_report(dispenser_controller_get_state() == DISPENSER_DONE);
    meal_scheduler_update();
    meal_t repas;
    while (meal_scheduler_take_due(&repas)) {
        if (arret || manual_badge_is_active() || !safety_manager_can_feed(repas.grammes)) {
            ESP_LOGW("SCHEDULER", "Repas %u ignore : moteur occupe ou securite", repas.identifiant);
            continue;
        }
        if (meal_scheduler_claim(repas.identifiant) != ESP_OK) {
            ESP_LOGE("SCHEDULER", "Repas %u non lance : date non sauvegardee", repas.identifiant);
            continue;
        }
        dispenser_controller_request(repas.grammes);
        ESP_LOGI("SCHEDULER", "Demande transmise au distributeur : repas %u", repas.identifiant);
    }
}

/* Affiche l etat de la balance au maximum une fois par seconde. */
static void afficher_balance(void)
{
    TickType_t maintenant = xTaskGetTickCount();
    if (maintenant - dernier_affichage < pdMS_TO_TICKS(1000)) return;
    dernier_affichage = maintenant;
    weight_snapshot_t poids = weight_manager_get_snapshot();
    if (!poids.sensor_ready) ESP_LOGW("Balance", "HX711 : aucune mesure disponible");
    else if (weight_manager_is_busy()) ESP_LOGI("Balance", "Tare ou calibration en cours...");
    else if (!poids.calibrated || !poids.tared)
        ESP_LOGI("Balance", "Masse indisponible : effectuer TARE puis CALIBRATE");
    else ESP_LOGI("Balance", "Masse : %.1f g", (double)poids.grams);
}

/* Fait avancer les modules et donne la priorite aux arrets. */
void app_controller_update(void)
{
    bool arret = false;
    if (fonctions.activer_ble) {
        arret = ble_manager_tick();
    }
    safety_manager_tick();
    bool defaut = safety_manager_get_fault() != SAFETY_FAULT_NONE;
    if (arret || defaut) dispenser_controller_stop();
    if (fonctions.activer_balance) {
        weight_manager_update();
        dispenser_controller_tick();
    }
    /* Distribution deja active prioritaire. Si personne ne tourne, un repas echu
     * est examine avant un nouveau demarrage par badge. */
    if (fonctions.activer_scheduler) {
        traiter_repas(arret || defaut);
    }
    /* Le badge ne prend le moteur que si la distribution et la balance le laissent disponible. */
    bool disponible = fonctions.activer_balance && !defaut && !dispenser_controller_is_busy()
        && (!weight_manager_is_busy() || manual_badge_is_taring());
    if (fonctions.activer_badge_manuel) {
        manual_badge_update(disponible, arret, !fonctions.badge_sans_tare);
    }
    if (fonctions.activer_balance && fonctions.afficher_balance) {
        afficher_balance();
    }
}
