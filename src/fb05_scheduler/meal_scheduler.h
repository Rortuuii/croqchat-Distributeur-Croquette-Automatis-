/* Planning persistant, synchronisation de l’heure et traitement des échéances. */
/* Gestion de l heure et des repas programmes. */
#ifndef MEAL_SCHEDULER_H
#define MEAL_SCHEDULER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    uint8_t identifiant;
    uint8_t heure;
    uint8_t minute;
    float grammes;
    bool active;
} meal_t;
esp_err_t meal_scheduler_init(void);
esp_err_t meal_scheduler_set(const meal_t *repas);
esp_err_t meal_scheduler_delete(uint8_t identifiant);
size_t meal_scheduler_get_meals(meal_t *liste_repas, size_t capacite);
esp_err_t meal_scheduler_set_time(int64_t horodatage, int16_t decalage_minutes);
bool meal_scheduler_clock_valid(void);
/* Appels depuis le coordinateur uniquement (pas depuis une tache BLE). */
void meal_scheduler_update(void);
/* Prend une echeance, sans la marquer executee tant qu'elle n'est pas acceptee. */
bool meal_scheduler_take_due(meal_t *repas);
/* Sauvegarde AVANT la demande moteur : si la NVS echoue, ne pas distribuer. */
esp_err_t meal_scheduler_claim(uint8_t identifiant);
void meal_scheduler_report(bool succes);
#endif
