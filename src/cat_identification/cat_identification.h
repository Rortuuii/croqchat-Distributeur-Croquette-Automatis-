/* Lecture du badge RFID et suivi de sa presence. */
#ifndef CAT_IDENTIFICATION_H
#define CAT_IDENTIFICATION_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool badge_present;
    char identifiant_badge[33];
} cat_identification_snapshot_t;

esp_err_t cat_identification_init(void);
/* Presence actualisee environ toutes les 200 ms ; lecture perimee apres 2 s. */
cat_identification_snapshot_t cat_identification_get_snapshot(void);

#endif
