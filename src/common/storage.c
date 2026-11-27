/* Ouverture de la memoire permanente qui conserve les reglages. */
#include "storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
/* Ouvre la memoire NVS sans effacer les reglages en cas d erreur. */
/* Ouvre la NVS utilisée par la balance et le planning. Réinitialise la partition si son format est incompatible ou si elle manque de place. */
esp_err_t storage_init(void)
{
    esp_err_t erreur = nvs_flash_init();
    if (erreur != ESP_OK) {
        ESP_LOGE("Storage", "NVS indisponible : %s (aucun effacement automatique)", esp_err_to_name(erreur));
    }
    return erreur;
}
