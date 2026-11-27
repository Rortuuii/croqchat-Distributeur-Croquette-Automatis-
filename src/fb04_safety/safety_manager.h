/* Défauts de sécurité et conditions de démarrage ou de réarmement. */
/* Surveillance du moteur et blocage des distributions en cas de defaut. */
#ifndef SAFETY_MANAGER_H
#define SAFETY_MANAGER_H
#include "esp_err.h"
#include <stdbool.h>
typedef enum {
    SAFETY_FAULT_NONE = 0, SAFETY_FAULT_SENSOR, SAFETY_FAULT_UNCALIBRATED,
    SAFETY_FAULT_INVALID_PORTION, SAFETY_FAULT_TIMEOUT, SAFETY_FAULT_NO_FLOW,
    SAFETY_FAULT_DISTRIBUTION
} safety_fault_t;
esp_err_t safety_manager_init(void);
void safety_manager_tick(void);
void safety_manager_request_stop(void);
void safety_manager_latch_fault(safety_fault_t defaut);
safety_fault_t safety_manager_get_fault(void);
/* Autorisation commune aux demandes manuelles et programmees. */
bool safety_manager_can_feed(float grammes);
esp_err_t safety_manager_reset_fault(void);
#endif
