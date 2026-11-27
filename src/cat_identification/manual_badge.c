/* Commande manuelle du moteur par badge, avec tare optionnelle avant demarrage. */
#include "manual_badge.h"
#include "cat_identification.h"
#include "motor_controller.h"
#include "weight_manager.h"
#include "croqchat_config.h"
#include "esp_log.h"


/* Le mode distingue la préparation de tare de la rotation. Un STOP reste mémorisé jusqu’au retrait du badge. */
static bool mode_active;
static bool actif, bloque_par_stop;
static bool preparation;
static uint32_t tare_depart;

/* Remet le mode badge au repos et choisit s il est active. */
bool manual_badge_init(bool activer)
{
    if (actif) motor_controller_stop();
    if (preparation) weight_manager_cancel_operation();
    preparation = false;
    actif = bloque_par_stop = false;
    mode_active = activer;
    return mode_active;
}

/* Attend la tare si demandee, puis commande le moteur tant que le badge est autorise. */
void manual_badge_update(bool disponible, bool arret_demande, bool tare_avant_demarrage)
{
    cat_identification_snapshot_t badge = cat_identification_get_snapshot();
    if (!badge.badge_present) bloque_par_stop = false;
    if (arret_demande && badge.badge_present) bloque_par_stop = true;

    bool tourne = mode_active && badge.badge_present && disponible && !bloque_par_stop && !arret_demande;
    if (preparation) {
        if (!tourne) {
            weight_manager_cancel_operation();
            preparation = false;
            return;
        }
        if (weight_manager_is_busy()) return;
        preparation = false;
        if (weight_manager_tare_count() == tare_depart) {
            bloque_par_stop = true;
            ESP_LOGE("BadgeManuel", "Moteur bloque : tare echouee, retirer le badge pour reessayer");
            return;
        }
        ESP_LOGI("BadgeManuel", "Tare terminee");
    } else if (tourne && !actif && tare_avant_demarrage) {
        tare_depart = weight_manager_tare_count();
        if (!weight_manager_demander_tare()) {
            bloque_par_stop = true;
            ESP_LOGE("BadgeManuel", "Moteur bloque : tare refusee");
            return;
        }
        preparation = true;
        ESP_LOGI("BadgeManuel", "Tare automatique avant demarrage");
        return;
    }
    if (tourne != actif) {
        ESP_LOGI("BadgeManuel", "%s", tourne ? "Carte detectee : moteur en marche"
                                             : "Retrait, STOP ou indisponibilite : moteur arrete");
    }
    /* Ne pas arreter le moteur si le mode badge n'en etait pas proprietaire. */
    if (tourne || actif) motor_controller_update(tourne, CROQCHAT_VITESSE_BADGE);
    actif = tourne;
}

/* Indique si le badge prepare ou commande une rotation. */
bool manual_badge_is_active(void) { return actif || preparation; }
/* Indique si le mode badge attend la fin de sa tare. */
bool manual_badge_is_taring(void) { return preparation; }
