/* Gestion de l heure et des repas programmes. */
#include "meal_scheduler.h"
#include "croqchat_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <math.h>
#include <string.h>

#define VERSION_PLANNING 1U
#define FENETRE_RETARD_S 60
/* Le decalage du telephone ne predit pas les changements d'heure. */
#define VALIDITE_HORLOGE_US (INT64_C(7) * 86400 * 1000000)
typedef struct {
    uint32_t version;
    meal_t repas[CROQCHAT_MEAL_COUNT]; /* ID=0 : emplacement vide. */
    int64_t dernier_jour[CROQCHAT_MEAL_COUNT]; /* -1 : jamais declenche. */
} sauvegarde_t;
static sauvegarde_t planning;
static bool disponible, synchronisee;
/* L’heure reçue en BLE sert de référence. Le compteur monotone fournit le temps écoulé depuis la synchronisation. */
static int64_t reference_utc, reference_us, derniere_verification;
static int16_t decalage;
static bool echeances[CROQCHAT_MEAL_COUNT];
static uint8_t en_cours;

/* Verifie l identifiant, l horaire et la quantite d un repas. */
static bool repas_valide(const meal_t *r)
{
    return r && r->identifiant >= 1 && r->identifiant <= CROQCHAT_MEAL_COUNT
        && r->heure < 24 && r->minute < 60 && isfinite(r->grammes)
        && r->grammes >= 1 && r->grammes <= 200;
}
/* Enregistre les reglages dans la memoire permanente. */
static esp_err_t sauvegarder(const sauvegarde_t *nouveau)
{
    nvs_handle_t memoire;
    esp_err_t e = nvs_open("croq_repas", NVS_READWRITE, &memoire);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(memoire, "planning", nouveau, sizeof(*nouveau));
    if (e == ESP_OK) e = nvs_commit(memoire);
    nvs_close(memoire);
    if (e != ESP_OK) ESP_LOGE("SCHEDULER", "Sauvegarde refusee : %s", esp_err_to_name(e));
    else planning = *nouveau;
    return e;
}
/* Recharge le planning sauvegarde et prepare le suivi des repas. */
esp_err_t meal_scheduler_init(void)
{
    ESP_LOGI("SCHEDULER", "Initialisation");
    memset(&planning, 0, sizeof(planning));
    planning.version = VERSION_PLANNING;
    for (unsigned i = 0; i < CROQCHAT_MEAL_COUNT; ++i) planning.dernier_jour[i] = -1;
    disponible = synchronisee = false; /* Pas de restauration de l'heure apres coupure. */
    en_cours = 0;
    memset(echeances, 0, sizeof(echeances));
    nvs_handle_t memoire;
    esp_err_t e = nvs_open("croq_repas", NVS_READWRITE, &memoire);
    if (e != ESP_OK) return e;
    sauvegarde_t charge = {0};
    size_t taille = sizeof(charge);
    e = nvs_get_blob(memoire, "planning", &charge, &taille);
    nvs_close(memoire);
    if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) return e;
    if (e == ESP_OK) {
        if (taille != sizeof(charge) || charge.version != VERSION_PLANNING) return ESP_ERR_INVALID_STATE;
        for (unsigned i = 0; i < CROQCHAT_MEAL_COUNT; ++i) {
            if (charge.dernier_jour[i] < -1 || charge.dernier_jour[i] > INT64_C(2932897)
                || (charge.repas[i].identifiant && (!repas_valide(&charge.repas[i])
                    || charge.repas[i].identifiant != i + 1))) return ESP_ERR_INVALID_STATE;
        }
        planning = charge;
    }
    disponible = true;
    unsigned compte = 0;
    for (unsigned i = 0; i < CROQCHAT_MEAL_COUNT; ++i) compte += planning.repas[i].identifiant != 0;
    ESP_LOGI("SCHEDULER", "%u repas charges depuis la NVS ; horloge inconnue", compte);
    return ESP_OK;
}
/* Ajoute ou modifie un repas dans le planning sauvegarde. */
esp_err_t meal_scheduler_set(const meal_t *repas)
{
    if (!repas_valide(repas)) return ESP_ERR_INVALID_ARG;
    if (!disponible) return ESP_ERR_INVALID_STATE;
    sauvegarde_t nouveau = planning;
    unsigned i = repas->identifiant - 1;
    nouveau.repas[i] = *repas; /* Garder la date anti-doublon lors d'une modification. */
    esp_err_t e = sauvegarder(&nouveau);
    if (e == ESP_OK) {
        echeances[i] = false;
        ESP_LOGI("SCHEDULER", "Repas %u : %02u:%02u, %.3f g, actif=%d", repas->identifiant,
                 repas->heure, repas->minute, (double)repas->grammes, repas->active);
    }
    return e;
}
/* Supprime un repas du planning sauvegarde. */
esp_err_t meal_scheduler_delete(uint8_t identifiant)
{
    if (identifiant < 1 || identifiant > CROQCHAT_MEAL_COUNT) return ESP_ERR_INVALID_ARG;
    if (!disponible) return ESP_ERR_INVALID_STATE;
    sauvegarde_t nouveau = planning;
    unsigned i = identifiant - 1;
    memset(&nouveau.repas[i], 0, sizeof(nouveau.repas[i]));
    /* Conserver la date si cet ID est recree aujourd'hui. */
    esp_err_t e = sauvegarder(&nouveau);
    if (e == ESP_OK) {
        echeances[i] = false;
        ESP_LOGI("SCHEDULER", "Repas %u supprime", identifiant);
    }
    return e;
}
/* Copie les repas sans depasser la capacite de la liste fournie. */
size_t meal_scheduler_get_meals(meal_t *liste, size_t capacite)
{
    if (!disponible || !liste) return 0;
    size_t n = 0;
    for (unsigned i = 0; i < CROQCHAT_MEAL_COUNT && n < capacite; ++i)
        if (planning.repas[i].identifiant) liste[n++] = planning.repas[i];
    return n;
}
/* Indique si l heure a ete reglee. */
bool meal_scheduler_clock_valid(void)
{
    if (!synchronisee) return false;
    int64_t age = esp_timer_get_time() - reference_us;
    return age >= 0 && age < VALIDITE_HORLOGE_US;
}
/* Calcule l heure locale avec le decalage horaire configure. */
static int64_t heure_locale(void)
{
    return reference_utc + (esp_timer_get_time() - reference_us) / 1000000 + (int64_t)decalage * 60;
}
/* Regle l heure et le decalage horaire utilises pour les repas. */
esp_err_t meal_scheduler_set_time(int64_t horodatage, int16_t decalage_minutes)
{
    if (horodatage < 1 || horodatage > INT64_C(253402300799)
        || decalage_minutes < -840 || decalage_minutes > 840
        || horodatage + (int64_t)decalage_minutes * 60 < 0) return ESP_ERR_INVALID_ARG;
    reference_utc = horodatage;
    reference_us = esp_timer_get_time();
    decalage = decalage_minutes;
    synchronisee = true;
    derniere_verification = heure_locale(); /* Aucun rattrapage apres synchronisation. */
    memset(echeances, 0, sizeof(echeances));
    int64_t s = derniere_verification % 86400;
    ESP_LOGI("CLOCK", "Heure synchronisee par BLE : %02u:%02u:%02u, UTC%+d min ; validite 7 jours",
             (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60), decalage);
    return ESP_OK;
}
/* Recherche les repas arrives a leur horaire. */
void meal_scheduler_update(void)
{
    if (!disponible || !meal_scheduler_clock_valid()) {
        memset(echeances, 0, sizeof(echeances));
        if (synchronisee) {
            synchronisee = false;
            ESP_LOGW("CLOCK", "Horloge expiree : synchroniser le telephone (decalage saisonnier)");
        }
        return;
    }
    int64_t maintenant = heure_locale();
    if (maintenant <= derniere_verification) return;
    int64_t jour = maintenant / 86400;
    for (unsigned i = 0; i < CROQCHAT_MEAL_COUNT; ++i) {
        meal_t *r = &planning.repas[i];
        int64_t echeance = jour * 86400 + r->heure * 3600 + r->minute * 60;
        if (r->identifiant && r->active && planning.dernier_jour[i] < jour
            && echeance > derniere_verification && echeance <= maintenant
            && maintenant - echeance < FENETRE_RETARD_S) {
            echeances[i] = true;
            ESP_LOGI("SCHEDULER", "Repas %u detecte : %02u:%02u, portion %.3f g",
                     r->identifiant, r->heure, r->minute, (double)r->grammes);
        }
    }
    derniere_verification = maintenant;
}
/* Recupere un repas en attente de distribution. */
bool meal_scheduler_take_due(meal_t *repas)
{
    if (!repas || !disponible || !meal_scheduler_clock_valid()) return false;
    int64_t maintenant = heure_locale();
    for (unsigned i = 0; i < CROQCHAT_MEAL_COUNT; ++i) {
        if (!echeances[i]) continue;
        echeances[i] = false; /* Consommer aussi un repas refuse : pas de rattrapage. */
        int64_t echeance = maintenant / 86400 * 86400
            + planning.repas[i].heure * 3600 + planning.repas[i].minute * 60;
        if (maintenant < echeance || maintenant - echeance >= FENETRE_RETARD_S) continue;
        *repas = planning.repas[i];
        return true;
    }
    return false;
}
/* Enregistre la prise en charge pour eviter de lancer deux fois le repas. */
/* Enregistre le jour d’exécution avant de démarrer la distribution pour éviter un second déclenchement le même jour. */
esp_err_t meal_scheduler_claim(uint8_t identifiant)
{
    if (!disponible || !meal_scheduler_clock_valid() || en_cours) return ESP_ERR_INVALID_STATE;
    if (identifiant < 1 || identifiant > CROQCHAT_MEAL_COUNT) return ESP_ERR_INVALID_ARG;
    unsigned i = identifiant - 1;
    int64_t maintenant = heure_locale(), jour = maintenant / 86400;
    meal_t *r = &planning.repas[i];
    int64_t echeance = jour * 86400 + r->heure * 3600 + r->minute * 60;
    if (!r->identifiant || !r->active || planning.dernier_jour[i] >= jour
        || maintenant < echeance || maintenant - echeance >= FENETRE_RETARD_S) return ESP_ERR_INVALID_STATE;
    sauvegarde_t nouveau = planning;
    nouveau.dernier_jour[i] = jour;
    esp_err_t e = sauvegarder(&nouveau);
    if (e == ESP_OK) {
        en_cours = identifiant;
    }
    return e;
}
/* Signale le resultat de la distribution au planning. */
void meal_scheduler_report(bool succes)
{
    if (!en_cours) return;
    ESP_LOGI("SCHEDULER", "Repas %u %s", en_cours, succes ? "execute" : "interrompu / en defaut");
    en_cours = 0;
}
