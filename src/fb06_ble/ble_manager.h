/* Interface BLE, sessions de connexion et traitement des commandes dans la boucle applicative. */
/* Connexion Bluetooth et transport des messages. */
#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ble_protocol.h"
typedef struct {
    protocol_command_t commande;
    uint32_t session;
} ble_command_event_t;
/* Retourne false si la file applicative refuse la commande. Appel depuis la tache BLE RX. */
typedef bool (*ble_command_handler_t)(const ble_command_event_t *evenement);
/* Interface simple pour main : demarrage et traitement non bloquant des commandes. */
esp_err_t ble_manager_start(void);
/* Capacites activees dans main.c ; les commandes des fonctions coupees sont refusees. */
void ble_manager_set_features(bool balance, bool scheduler);
/* Retourne true lorsqu une commande STOP a ete traitee. */
bool ble_manager_tick(void);
/* Interface bas niveau avec gestionnaire de commandes personnalise. */
esp_err_t ble_manager_init(ble_command_handler_t gestionnaire);
esp_err_t ble_manager_send_line(uint32_t session, const char *ligne);
bool ble_manager_session_active(uint32_t session);
#endif
