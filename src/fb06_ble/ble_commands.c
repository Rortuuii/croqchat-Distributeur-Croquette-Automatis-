/* Execution des commandes envoyees par l application en Bluetooth. */
/* Commandes applicatives BLE : files, reponses et priorite STOP.
 * Le traitement reste dans la boucle principale pour serialiser les acces aux blocs. */
#include "ble_manager.h"
#include "croqchat_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include "esp_log.h"
#include "weight_manager.h"
#include "motor_controller.h"
#include "dispenser_controller.h"
#include "safety_manager.h"
#include "meal_scheduler.h"
#include "cat_identification.h"
#include "manual_badge.h"

static const char *ETIQUETTE_JOURNAL = "BLECommands";
static QueueHandle_t file_commandes;
static QueueHandle_t file_arret;
static bool balance_active = true;
static bool scheduler_actif = true;
static bool operation_attendue;
static uint32_t session_operation;
static protocol_command_type_t type_operation;

static void terminer_operation(void)
{
    if (!operation_attendue || weight_manager_is_busy()) return;
    operation_attendue = false;
    if (!ble_manager_session_active(session_operation)) return;
    weight_snapshot_t poids = weight_manager_get_snapshot();
    const char *erreurs[] = {"", "BUSY", "SENSOR", "INVALID_CALIBRATION", "STORAGE", "CANCELLED"};
    char ligne[96];
    if (poids.operation_result == 0)
        snprintf(ligne, sizeof(ligne), "DONE;cmd=%s", protocol_command_name(type_operation));
    else snprintf(ligne, sizeof(ligne), "ERR;cmd=%s;code=%s", protocol_command_name(type_operation),
        poids.operation_result < 6 ? erreurs[poids.operation_result] : "OPERATION");
    ble_manager_send_line(session_operation, ligne);
}

/* Declare les fonctions disponibles pour leurs commandes Bluetooth. */
void ble_manager_set_features(bool balance, bool scheduler)
{
    balance_active = balance;
    scheduler_actif = scheduler;
}

/* Place une commande recue dans la file de traitement. */
static bool enqueue_command(const ble_command_event_t *evenement)
{
    /* STOP dispose d'une boite dediee, independante d'une saturation des commandes. */
    if (evenement->commande.type == CMD_STOP) return xQueueOverwrite(file_arret, evenement) == pdTRUE;
    return xQueueSend(file_commandes, evenement, 0) == pdTRUE;
}
/* Envoie une reponse a la session Bluetooth concernee. */
static void reply(uint32_t session, const char *ligne)
{
    esp_err_t erreur = ble_manager_send_line(session, ligne);
    if (erreur != ESP_OK) ESP_LOGW(ETIQUETTE_JOURNAL, "Reponse non transmise : %s", esp_err_to_name(erreur));
}
/* Execute la commande et prepare la reponse pour l application. */
static void handle_command(const ble_command_event_t *evenement)
{
    if (!ble_manager_session_active(evenement->session)) return;
    const protocol_command_t *commande = &evenement->commande;
    bool commande_balance = commande->type == CMD_MANUAL_FEED || commande->type == CMD_TARE
        || commande->type == CMD_CALIBRATE;
    bool commande_scheduler = commande->type == CMD_SET_MEAL || commande->type == CMD_DELETE_MEAL
        || commande->type == CMD_GET_MEALS || commande->type == CMD_SET_TIME;
    if ((!balance_active && commande_balance) || (!scheduler_actif && commande_scheduler)) {
        reply(evenement->session, "ERR;code=DISABLED");
        return;
    }
    if (commande->type == CMD_GET_STATUS) {
        weight_snapshot_t poids = weight_manager_get_snapshot();
        cat_identification_snapshot_t chat = cat_identification_get_snapshot();
        char etat_texte[CROQCHAT_TX_LINE_MAX];
        snprintf(etat_texte, sizeof(etat_texte),
                 "STATUS;w=%.1f;busy=%d;fault=%d;sensor=%d;cal=%d;tare=%d;clock=%d;rfid=%s;last=%.1f;scale=%d;schedule=%d;op=%u",
                 (double)poids.grams, (weight_manager_is_busy() || dispenser_controller_is_busy() || motor_controller_is_running()), safety_manager_get_fault(),
                 poids.sensor_ready, poids.calibrated, poids.tared,
                 meal_scheduler_clock_valid(), chat.badge_present ? chat.identifiant_badge : "none",
                 (double)dispenser_controller_last_mass(), balance_active, scheduler_actif, poids.operation_result);
        reply(evenement->session, "ACK;cmd=GET_STATUS");
        reply(evenement->session, etat_texte);
    } else if (commande->type == CMD_RESET_FAULT) {
        reply(evenement->session, safety_manager_reset_fault() == ESP_OK
            ? "ACK;cmd=RESET_FAULT" : "ERR;code=BUSY_OR_SENSOR");
    } else if (commande->type == CMD_GET_MEALS) {
        reply(evenement->session, "ACK;cmd=GET_MEALS");
        reply(evenement->session, "MEALS_BEGIN");
        meal_t repas[CROQCHAT_MEAL_COUNT];
        size_t nombre = meal_scheduler_get_meals(repas, CROQCHAT_MEAL_COUNT);
        for (size_t i = 0; i < nombre; ++i) {
            char ligne[96];
            snprintf(ligne, sizeof(ligne), "MEAL;id=%u;time=%02u:%02u;mass=%.9g;enable=%d",
                     repas[i].identifiant, repas[i].heure, repas[i].minute,
                     (double)repas[i].grammes, repas[i].active);
            reply(evenement->session, ligne);
        }
        reply(evenement->session, "MEALS_END");
    } else if (commande->type == CMD_SET_MEAL) {
        meal_t repas = {.identifiant = commande->identifiant_repas, .heure = commande->heure,
            .minute = commande->minute, .grammes = commande->grammes, .active = commande->active};
        esp_err_t erreur = meal_scheduler_set(&repas);
        reply(evenement->session, erreur == ESP_OK ? "ACK;cmd=SET_MEAL"
              : erreur == ESP_ERR_INVALID_ARG ? "ERR;code=INVALID_MEAL" : "ERR;code=STORAGE");
    } else if (commande->type == CMD_DELETE_MEAL) {
        esp_err_t erreur = meal_scheduler_delete(commande->identifiant_repas);
        reply(evenement->session, erreur == ESP_OK ? "ACK;cmd=DELETE_MEAL" : "ERR;code=STORAGE");
    } else if (commande->type == CMD_SET_TIME) {
        esp_err_t erreur = meal_scheduler_set_time(commande->horodatage, commande->decalage_minutes);
        reply(evenement->session, erreur == ESP_OK ? "ACK;cmd=SET_TIME" : "ERR;code=INVALID_TIME");
    } else if (commande->type == CMD_MANUAL_FEED) {
        /* La commande BLE contient la quantite a ajouter, en grammes.
         * Demarrer une seule fois : main fera ensuite avancer le distributeur. */
        if (dispenser_controller_is_busy() || motor_controller_is_running() || weight_manager_is_busy()) {
            reply(evenement->session, "ERR;code=BUSY");
            return;
        }
        if (!safety_manager_can_feed(commande->grammes)) {
            reply(evenement->session, "ERR;code=SAFETY");
            return;
        }
        dispenser_controller_request(commande->grammes);
        reply(evenement->session, "ACK;cmd=MANUAL_FEED");
    } else if (commande->type == CMD_TARE || commande->type == CMD_CALIBRATE) {
        if (dispenser_controller_is_busy() || manual_badge_is_active()) {
            reply(evenement->session, "ERR;code=BUSY");
            return;
        }
        bool calibration = commande->type == CMD_CALIBRATE;
        if (calibration && !weight_manager_get_snapshot().tared) {
            reply(evenement->session, "ERR;code=NOT_TARED");
            return;
        }
        bool acceptee = calibration ? weight_manager_demander_calibration(commande->grammes)
                                    : weight_manager_demander_tare();
        if (acceptee) {
            operation_attendue = true;
            session_operation = evenement->session;
            type_operation = commande->type;
        }
        reply(evenement->session, !acceptee ? "ERR;code=BUSY"
              : calibration ? "ACK;cmd=CALIBRATE" : "ACK;cmd=TARE");
    } else {
        ESP_LOGW(ETIQUETTE_JOURNAL, "%s validee mais non executee : diagnostic TEST_ONLY",
                 protocol_command_name(commande->type));
        reply(evenement->session, "ERR;code=TEST_ONLY");
    }
}
/* Recherche une demande STOP a traiter en priorite. */
static bool handle_priority_stop(void)
{
    ble_command_event_t evenement;
    if (xQueueReceive(file_arret, &evenement, 0) != pdTRUE) return false;
    // Toujours mettre les sorties en securite, meme si le telephone vient de se deconnecter.
    safety_manager_request_stop();
    weight_manager_cancel_operation();
    terminer_operation();
    ESP_LOGW(ETIQUETTE_JOURNAL, "STOP prioritaire : sorties moteur maintenues a zero");
    ble_command_event_t commande_annulee;
    for (unsigned indice = 0; indice < CROQCHAT_COMMAND_QUEUE_DEPTH; ++indice) {
        if (xQueueReceive(file_commandes, &commande_annulee, 0) != pdTRUE) break;
        if (ble_manager_session_active(commande_annulee.session)) reply(commande_annulee.session, "ERR;code=CANCELLED");
    }
    if (ble_manager_session_active(evenement.session)) reply(evenement.session, "ACK;cmd=STOP");
    return true;
}
/* Prepare la file des commandes et demarre le Bluetooth. */
esp_err_t ble_manager_start(void)
{
    if (file_commandes || file_arret) return ESP_ERR_INVALID_STATE;
    file_commandes = xQueueCreate(CROQCHAT_COMMAND_QUEUE_DEPTH, sizeof(ble_command_event_t));
    file_arret = xQueueCreate(1, sizeof(ble_command_event_t));
    if (!file_commandes || !file_arret) {
        if (file_commandes) vQueueDelete(file_commandes);
        if (file_arret) vQueueDelete(file_arret);
        file_commandes = NULL;
        file_arret = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ble_manager_init(enqueue_command);
}
/* Traite les commandes en attente depuis la boucle principale. */
bool ble_manager_tick(void)
{
    if (!file_commandes || !file_arret) return false;
    terminer_operation();
    bool arret_demande = handle_priority_stop();
    ble_command_event_t evenement;
    if (xQueueReceive(file_commandes, &evenement, 0) == pdTRUE) {
        if (handle_priority_stop()) {
            if (ble_manager_session_active(evenement.session)) reply(evenement.session, "ERR;code=CANCELLED");
            return true;
        }
        handle_command(&evenement);
    }
    return arret_demande;
}
