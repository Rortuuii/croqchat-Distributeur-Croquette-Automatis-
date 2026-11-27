/* Interface d’initialisation du stockage persistant NVS. */
/* Ouverture de la memoire permanente qui conserve les reglages. */
#ifndef CROQCHAT_STORAGE_H
#define CROQCHAT_STORAGE_H
#include "esp_err.h"
/* Initialise NVS sans effacer automatiquement les reglages existants. */
esp_err_t storage_init(void);
#endif
