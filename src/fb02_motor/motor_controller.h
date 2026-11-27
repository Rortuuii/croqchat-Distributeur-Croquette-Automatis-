/* Commande des quatre entrees du moteur pas a pas. */
#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H
#include <stdbool.h>
#include <stdint.h>

// Appeler regulierement, environ toutes les 1 ms, sans attente bloquante.
// tourne : 1 = tourner, 0 = arreter.
// vitesse : demi-pas par seconde (1 a 500). 0 arrete, au-dela limite a 500.
void motor_controller_update(bool tourne, uint32_t vitesse);
// Utilise par la securite et le STOP BLE.
void motor_controller_stop(void);
bool motor_controller_is_running(void);
#endif
