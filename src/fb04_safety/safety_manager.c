/* Surveillance du moteur et blocage des distributions en cas de defaut. */
#include "safety_manager.h"
#include "dispenser_controller.h"
#include "esp_log.h"
#include "weight_manager.h"
#include "motor_controller.h"
#include "croqchat_config.h"
#include "manual_badge.h"
#include "cat_identification.h"
#include "esp_timer.h"
#include <math.h>

/* Le délai sans variation surveille le débit. Un écart d’au moins 1 g par rapport à la référence prolonge la surveillance. */
#define DELAI_SANS_VARIATION_US INT64_C(10000000)
#define VARIATION_MIN_GRAMMES 1.0f

static bool surveillance_masse_active;
static int64_t echeance_masse_us;
static float masse_reference;
/* Le premier défaut reste mémorisé et bloque le moteur jusqu’au réarmement. */
static safety_fault_t defaut_actuel;
static bool surveillance_active;
static int64_t echeance_moteur_us;
/* Remet la surveillance de securite dans son etat initial. */
esp_err_t safety_manager_init(void)
{
    defaut_actuel = SAFETY_FAULT_NONE;
    surveillance_active = false;
    surveillance_masse_active = false;
    echeance_moteur_us = 0;
    ESP_LOGI("FB04", "Timeout moteur : %u ms (a regler apres essais)",
             (unsigned)CROQCHAT_TIMEOUT_MOTEUR_MS);
    return ESP_OK;
}
/* Surveille la duree de distribution et arrete le moteur au depassement du delai. */
void safety_manager_tick(void)
{
    if (defaut_actuel != SAFETY_FAULT_NONE) {
        motor_controller_stop();
        return;
    }
    /* Ne pas reinitialiser le delai pendant les pauses de la distribution fine.
     * La tare reste hors de cette fenetre ; elle est protegee par son propre delai. */
    bool distribution = dispenser_controller_is_busy()
        && dispenser_controller_get_state() != DISPENSER_STARTING;
    weight_snapshot_t poids = weight_manager_get_snapshot();
    if (distribution) {
        if (!poids.sensor_ready || !isfinite(poids.grams)) {
            safety_manager_latch_fault(SAFETY_FAULT_SENSOR);
            return;
        }
        if (!poids.calibrated || !poids.tared) {
            safety_manager_latch_fault(SAFETY_FAULT_UNCALIBRATED);
            return;
        }
    }
    bool badge = manual_badge_is_active() && !manual_badge_is_taring();
    bool moteur_en_marche = motor_controller_is_running();
    /* Le délai maximal couvre toute la distribution, y compris les pauses de la phase fine. */
    bool occupation = moteur_en_marche || distribution || badge;
    if (!occupation) surveillance_masse_active = false;
    if (!occupation) {
        surveillance_active = false;
        return;
    }
    int64_t maintenant = esp_timer_get_time();
    /* Prolonge le suivi pendant les pauses de la distribution fine. */
    if (moteur_en_marche || (distribution && surveillance_masse_active)) {
        float masse = poids.sensor_ready && poids.calibrated && poids.tared ? poids.grams : NAN;
        if (!surveillance_masse_active
            || (isfinite(masse) && (!isfinite(masse_reference)
                                    || fabsf(masse - masse_reference) >= VARIATION_MIN_GRAMMES))) {
            surveillance_masse_active = true;
            masse_reference = masse;
            echeance_masse_us = maintenant + DELAI_SANS_VARIATION_US;
        } else if (maintenant >= echeance_masse_us) {
            ESP_LOGE("FB04", "Moteur arrete : masse sans variation significative depuis 10 s");
            safety_manager_latch_fault(SAFETY_FAULT_NO_FLOW);
            return;
        }
    } else {
        surveillance_masse_active = false;
    }
    if (!surveillance_active) {
        echeance_moteur_us = maintenant + (int64_t)CROQCHAT_TIMEOUT_MOTEUR_MS * 1000;
        surveillance_active = true;
    } else if (maintenant >= echeance_moteur_us) {
        ESP_LOGE("FB04", "TIMEOUT MOTEUR : arret apres %u ms, verifier la balance ; REGLER ce delai",
                 (unsigned)CROQCHAT_TIMEOUT_MOTEUR_MS);
        surveillance_active = false;
        safety_manager_latch_fault(SAFETY_FAULT_TIMEOUT);
        motor_controller_stop();
    }
}
/* Transmet une demande d arret au distributeur. */
void safety_manager_request_stop(void) { dispenser_controller_stop(); }
/* Arrete la distribution et memorise le premier defaut. */
void safety_manager_latch_fault(safety_fault_t defaut)
{
    safety_manager_request_stop();
    if (defaut_actuel == SAFETY_FAULT_NONE) defaut_actuel = defaut;
}
/* Renvoie le defaut memorise. */
safety_fault_t safety_manager_get_fault(void) { return defaut_actuel; }
/* Verifie la quantite et la disponibilite de la balance et du moteur. */
bool safety_manager_can_feed(float grammes)
{
    weight_snapshot_t poids = weight_manager_get_snapshot();
    return defaut_actuel == SAFETY_FAULT_NONE && !dispenser_controller_is_busy()
        && !motor_controller_is_running() && !weight_manager_is_busy()
        && poids.sensor_ready && poids.calibrated && poids.tared && isfinite(poids.grams)
        && isfinite(grammes) && grammes > 0 && grammes <= CROQCHAT_MAX_MASS_GRAMS;
}

/* Le badge doit etre retire et aucune operation ne doit etre en cours. */
esp_err_t safety_manager_reset_fault(void)
{
    if (motor_controller_is_running() || dispenser_controller_is_busy()
        || manual_badge_is_active() || weight_manager_is_busy()
        || cat_identification_get_snapshot().badge_present) return ESP_ERR_INVALID_STATE;
    weight_snapshot_t poids = weight_manager_get_snapshot();
    if (defaut_actuel == SAFETY_FAULT_SENSOR && (!poids.sensor_ready || !isfinite(poids.grams)))
        return ESP_ERR_INVALID_STATE;
    motor_controller_stop();
    surveillance_active = false;
    surveillance_masse_active = false;
    echeance_moteur_us = 0;
    defaut_actuel = SAFETY_FAULT_NONE;
    return ESP_OK;
}
