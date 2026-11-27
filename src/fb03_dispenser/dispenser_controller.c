/* Distribution de la quantite demandee et suivi du poids. */
#include "dispenser_controller.h"
#include "motor_controller.h"
#include "weight_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <math.h>

/* Les durees sont en microsecondes : 100000 us = 100 ms. */
#define SEUIL_APPROCHE_GRAMMES 15.0f
#define SEUIL_IMPULSIONS_GRAMMES 5.0f
#define VITESSE_NORMALE 500U
#define VITESSE_APPROCHE (VITESSE_NORMALE / 2U)
#define DUREE_IMPULSION_US 100000
#define DUREE_PAUSE_US 200000

static dispenser_state_t etat = DISPENSER_IDLE;
static float masse_depart;
static float masse_cible; /* Quantite a ajouter, pas le poids total du bol. */
static bool tourne = false;
static int64_t prochaine_action = 0;
static float derniere_masse;
static uint32_t tare_depart;

/* Arrete le moteur et remet la distribution au repos. */
void dispenser_controller_init(void)
{
    motor_controller_stop();
    etat = DISPENSER_IDLE;
    masse_depart = 0.0f;
    masse_cible = 0.0f;
    tourne = false;
    prochaine_action = 0;
    derniere_masse = 0;

    ESP_LOGI("FB03", "Controleur de distribution initialise");
}

/* Verifie la demande et lance une tare avant de distribuer. */
void dispenser_controller_request(float masse_cible_grammes)
{
    if (dispenser_controller_is_busy() || weight_manager_is_busy()
        || motor_controller_is_running()) return;
    if (!isfinite(masse_cible_grammes) || masse_cible_grammes <= 0) return;
    masse_cible = masse_cible_grammes;

    /* Chaque demande recommence avec une commande moteur arretee. */
    tourne = false;
    motor_controller_stop();
    prochaine_action = 0;
    tare_depart = weight_manager_tare_count();
    if (!weight_manager_demander_tare()) {
        etat = DISPENSER_FAULT;
        ESP_LOGE("DISPENSER", "Distribution annulee : tare refusee");
        return;
    }
    etat = DISPENSER_STARTING;
    ESP_LOGI("DISPENSER", "Tare automatique avant distribution de %.3f g", (double)masse_cible);
}

/* Annule la distribution et arrete le moteur. */
void dispenser_controller_stop(void)
{
    if (dispenser_controller_is_busy()) ESP_LOGW("DISPENSER", "Distribution interrompue");
    if (etat == DISPENSER_STARTING) weight_manager_cancel_operation();
    tourne = false;
    motor_controller_stop();
    prochaine_action = 0;
    etat = DISPENSER_IDLE;
}

/* Suit le poids et alterne rotation et pauses pres de la cible. */
void dispenser_controller_tick(void)
{
    /* main appelle cette fonction regulierement, meme au repos. */
    if (!dispenser_controller_is_busy()) {
        return;
    }

    weight_snapshot_t poids = weight_manager_get_snapshot();
    if (etat == DISPENSER_STARTING) {
        /* Ne jamais confondre une ancienne tare avec celle de cette demande. */
        if (weight_manager_is_busy()) return;
        if (weight_manager_tare_count() == tare_depart || !poids.sensor_ready
            || !poids.calibrated || !poids.tared || !isfinite(poids.grams)) {
            motor_controller_stop();
            etat = DISPENSER_FAULT;
            ESP_LOGE("DISPENSER", "Distribution annulee : echec de la tare automatique");
            return;
        }
        masse_depart = poids.grams;
        etat = DISPENSER_FEEDING;
        ESP_LOGI("DISPENSER", "Tare terminee, distribution en cours : %.3f g", (double)masse_cible);
    }
    if (!poids.sensor_ready || !poids.calibrated || !poids.tared || !isfinite(poids.grams)) {
        motor_controller_stop();
        etat = DISPENSER_FAULT;
        return;
    }
    float masse_distribuee = poids.grams - masse_depart;
    float masse_restante = masse_cible - masse_distribuee;

    if (masse_restante <= 0.0f) {
        /* L'appel moteur en fin de fonction applique immediatement l'arret. */
        tourne = false;
        etat = DISPENSER_DONE;
        derniere_masse = masse_distribuee;
        ESP_LOGI("DISPENSER", "Distribution terminee : %.3f g", (double)derniere_masse);
    } else if (masse_restante > SEUIL_IMPULSIONS_GRAMMES
               && etat != DISPENSER_SETTLING) {
        /* Rotation continue jusqu'au seuil des impulsions.
         * Une fois en phase fine, on y reste meme si le poids fluctue. */
        tourne = true;
        etat = DISPENSER_FEEDING;
    } else {
        int64_t maintenant = esp_timer_get_time();

        /* SETTLING designe ici toute la phase fine : impulsions et pauses.
         * A l'entree, commencer par une pause pour stabiliser le poids. */
        if (etat != DISPENSER_SETTLING) {
            tourne = false;
            prochaine_action = maintenant + DUREE_PAUSE_US;
            etat = DISPENSER_SETTLING;
        } else if (maintenant >= prochaine_action) {
            /* Inverser la commande seulement lorsque la duree est ecoulee. */
            tourne = !tourne;
            prochaine_action = maintenant
                + (tourne ? DUREE_IMPULSION_US : DUREE_PAUSE_US);
        }
        /* Aucun delai bloquant : les autres modules continuent a travailler. */
    }

    uint32_t vitesse = VITESSE_NORMALE;
    if (etat == DISPENSER_FEEDING && masse_restante <= SEUIL_APPROCHE_GRAMMES) {
        vitesse = VITESSE_APPROCHE;
    }
    motor_controller_update(tourne, vitesse);
}

/* Renvoie l etape actuelle de la distribution. */
dispenser_state_t dispenser_controller_get_state(void)
{
    return etat;
}

/* Indique si une distribution se prepare ou est en cours. */
bool dispenser_controller_is_busy(void)
{
    return etat == DISPENSER_STARTING
        || etat == DISPENSER_FEEDING
        || etat == DISPENSER_SETTLING;
}
/* Renvoie la masse mesuree a la fin de la derniere distribution terminee. */
float dispenser_controller_last_mass(void) { return derniere_masse; }
