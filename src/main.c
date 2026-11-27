#include <stdbool.h>
#include <inttypes.h>
#include "croqchat_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_controller.h"
#include "motor_controller.h"
#include "cat_identification.h"
#include "esp_log.h"
#include "storage.h"
#include "weight_manager.h"

#define TEST_BALANCE_BRUTE 0 /* Remettre a 0 pour le fonctionnement normal. */

#define TEST_RFID_SEUL 0 /* CR95HF UART : affiche les UID dans le moniteur serie. */
#define TEST_MOTEUR_CONTINU 0
#define TEST_MOTEUR_VITESSE 500U /* Demi-pas par seconde, maximum 500. */

void app_main(void)
{
#if TEST_BALANCE_BRUTE
    motor_controller_update(false, 0);
    ESP_ERROR_CHECK(storage_init());
    weight_manager_init();
    ESP_LOGI("TestBalance", "HX711 : DOUT=GPIO%d, SCK=GPIO%d",
             CROQCHAT_GPIO_HX711_DOUT, CROQCHAT_GPIO_HX711_SCK);

    TickType_t dernier_affichage = xTaskGetTickCount();
    while (true) {
        weight_manager_update();
        TickType_t maintenant = xTaskGetTickCount();
        if (maintenant - dernier_affichage >= pdMS_TO_TICKS(200)) {
            dernier_affichage = maintenant;
            weight_snapshot_t mesure = weight_manager_get_snapshot();
            if (mesure.sensor_ready) {
                ESP_LOGI("TestBalance", "brut=%" PRId32, mesure.raw);
            } else {
                ESP_LOGW("TestBalance", "HX711 : aucune conversion recue");
            }
        }
        vTaskDelay(1);
    }
#elif TEST_RFID_SEUL
    motor_controller_update(false, 0);
    ESP_LOGI("TestRFID", "Test ESP32 / CR95HF : RX=%d, TX=%d",
             CROQCHAT_GPIO_RFID_RX, CROQCHAT_GPIO_RFID_TX);
    esp_err_t resultat = cat_identification_init();
    if (resultat != ESP_OK) {
        ESP_LOGE("TestRFID", "Initialisation impossible : %s. Verifier alimentation, mode UART et cablage.",
                 esp_err_to_name(resultat));
        return;
    }
    ESP_LOGI("TestRFID", "Presenter un badge : son UID sera affiche par la tache RFID.");
    return; /* La tache RFID continue de fonctionner. */
#elif TEST_MOTEUR_CONTINU
    /* Test autonome : rotation des le demarrage, sans balance ni BLE.
     * Remettre TEST_MOTEUR_CONTINU a 0 pour retrouver le fonctionnement normal.
     * Pour arreter ce test, couper l'alimentation du moteur. */
    ESP_LOGI("TestMoteur", "Rotation continue : %u demi-pas/s",
             (unsigned)TEST_MOTEUR_VITESSE);
    while (true) {
        motor_controller_update(true, TEST_MOTEUR_VITESSE);
        vTaskDelay(1); /* Un tick pour laisser les autres taches fonctionner. */
    }
#else
    motor_controller_update(false, 0);
    const app_options_t options = {
        .activer_ble = true,
        .activer_balance = true,
        .activer_scheduler = true,
#if !CROQCHAT_DIAGNOSTIC_ONLY
        .activer_rfid = true,
        .activer_badge_manuel = true,
        .badge_sans_tare = true,
#endif
        .afficher_balance = true,
    };
    app_controller_init(&options);
    while (true) {
        app_controller_update();
        vTaskDelay(1);
    }
#endif /* Modes de test */
}
