/* Options de démarrage et points d’entrée du coordinateur de l’application. */
/* Coordination des modules et des modes choisis dans main.c. */
#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H
#include <stdbool.h>
/* Regles au demarrage dans main.c ; recompiler apres une modification. */
typedef struct {
    bool activer_ble;
    bool activer_balance;
    bool activer_scheduler;
    bool activer_rfid;
    bool activer_badge_manuel;
    bool badge_sans_tare;
    bool afficher_balance;
    bool test_moteur_continu;
} app_options_t;
void app_controller_init(const app_options_t *options);
/* Un tour de boucle : BLE, balance, distribution, planning et commande par badge. */
void app_controller_update(void);
#endif
