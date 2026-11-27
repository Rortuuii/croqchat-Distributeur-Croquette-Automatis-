/* Lecture du HX711, filtrage du poids, tare et calibration. */
#ifndef WEIGHT_MANAGER_H
#define WEIGHT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float grams;
    int32_t raw; /* Dernier echantillon signe, valide si sensor_ready. */
    bool sensor_ready;
    bool calibrated;
    bool tared;
    /* 0=succes/repos, 1=en cours, 2=capteur, 3=calibration, 4=NVS, 5=annule. */
    unsigned operation_result;
} weight_snapshot_t;

void weight_manager_init(void);
void weight_manager_update(void);
weight_snapshot_t weight_manager_get_snapshot(void);
bool weight_manager_is_busy(void);
/* Numero incremente uniquement apres une tare terminee avec succes. */
uint32_t weight_manager_tare_count(void);
void weight_manager_cancel_operation(void);

/* Non bloquant : update collecte 20 mesures pour terminer la demande. */
bool weight_manager_demander_tare(void);
bool weight_manager_demander_calibration(float masse_reference);

#endif
