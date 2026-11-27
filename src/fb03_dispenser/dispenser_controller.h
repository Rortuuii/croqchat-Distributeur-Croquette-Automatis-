/* Distribution de la quantite demandee et suivi du poids. */
#ifndef DISPENSER_CONTROLLER_H
#define DISPENSER_CONTROLLER_H
#include <stdbool.h>
typedef enum {
    DISPENSER_IDLE, DISPENSER_STARTING, DISPENSER_FEEDING,
    DISPENSER_SETTLING, DISPENSER_DONE, DISPENSER_FAULT
} dispenser_state_t;
void dispenser_controller_init(void);
/* La demande commence par une tare moteur arrete ; STARTING couvre cette attente. */
void dispenser_controller_request(float masse_cible_grammes);
void dispenser_controller_stop(void);
void dispenser_controller_tick(void);
dispenser_state_t dispenser_controller_get_state(void);
bool dispenser_controller_is_busy(void);
float dispenser_controller_last_mass(void);
#endif
