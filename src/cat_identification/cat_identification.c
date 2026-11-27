/* Lecture du badge RFID et suivi de sa presence. */
#include "cat_identification.h"
#include "croqchat_config.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define PORT_RFID UART_NUM_2
/* Une lecture valide prolonge la présence pendant deux secondes, même si quelques interrogations échouent. */
#define DELAI_PERTE_BADGE_US INT64_C(2000000)

static bool uart_initialise;
static portMUX_TYPE verrou_badge = portMUX_INITIALIZER_UNLOCKED;
static cat_identification_snapshot_t badge;
static int64_t derniere_lecture_us;

/* Partage entre la tache RFID et la boucle principale, sans UID partiellement copie. */
static void publier_badge(const char *uid)
{
    portENTER_CRITICAL(&verrou_badge);
    bool change = uid ? !badge.badge_present || strcmp(badge.identifiant_badge, uid)
                      : badge.badge_present;
    badge.badge_present = uid != NULL;
    snprintf(badge.identifiant_badge, sizeof(badge.identifiant_badge), "%s", uid ? uid : "");
    derniere_lecture_us = esp_timer_get_time();
    portEXIT_CRITICAL(&verrou_badge);
    if (change) {
        if (uid) ESP_LOGI("RFID", "Badge present (UID : %s)", uid);
        else ESP_LOGI("RFID", "Badge absent (retrait ou lecture perdue)");
    }
}

/* Envoyer une commande et recevoir sa reponse (32 octets maximum).
 * Retourner la longueur recue, ou -1 si la reponse n'est pas valide.
 */
static int envoyer_commande(const uint8_t *commande, int taille, uint8_t reponse[32])
{
    uart_write_bytes(PORT_RFID, commande, taille);
    uint8_t entete[2];
    if (uart_read_bytes(PORT_RFID, entete, 2, pdMS_TO_TICKS(100)) != 2) return -1;
    int longueur = entete[1];
    if (longueur > 32 || (entete[0] & 0x60) != 0) {
        uart_flush_input(PORT_RFID);
        return -1;
    }
    if (longueur > 0 && uart_read_bytes(PORT_RFID, reponse, longueur,
                                       pdMS_TO_TICKS(100)) != longueur) return -1;
    if (entete[0] != 0x00 && entete[0] != 0x80) return -1;
    return longueur;
}

/* Lire les niveaux successifs pour obtenir un UID de 4, 7 ou 10 octets. */
static int lire_uid(uint8_t uid[10])
{
    uint8_t reponse[32];
    int longueur_uid = 0;
    for (int niveau = 0; niveau < 3; niveau++) {
        uint8_t code_niveau = 0x93 + 2 * niveau;
        uint8_t anticollision[] = {0x04, 0x03, code_niveau, 0x20, 0x08};
        if (envoyer_commande(anticollision, sizeof(anticollision), reponse) != 8) return 0;
        if (reponse[5] != 0x28 || reponse[6] != 0 || reponse[7] != 0) return 0;
        if ((reponse[0] ^ reponse[1] ^ reponse[2] ^ reponse[3]) != reponse[4]) return 0;

        uint8_t selection[] = {
            0x04, 0x08, code_niveau, 0x70,
            reponse[0], reponse[1], reponse[2], reponse[3], reponse[4], 0x28
        };
        uint8_t bloc[4] = {reponse[0], reponse[1], reponse[2], reponse[3]};
        if (envoyer_commande(selection, sizeof(selection), reponse) != 6) return 0;
        if (reponse[3] != 0x08 || reponse[4] != 0 || reponse[5] != 0) return 0;

        bool suite = (reponse[0] & 0x04) != 0;
        if (suite && (niveau == 2 || bloc[0] != 0x88)) return 0;
        if (!suite && niveau < 2 && bloc[0] == 0x88) return 0;
        int debut = 0;
        if (suite) debut = 1; /* Ne pas conserver le marqueur de cascade 0x88. */
        for (int indice = debut; indice < 4; indice++) {
            uid[longueur_uid++] = bloc[indice];
        }
        if (!suite) return longueur_uid;
    }
    return 0;
}

/* Recherche une carte et convertit son identifiant en texte hexadecimal. */
static bool rechercher_badge(char texte_uid[21])
{
    const uint8_t arret_champ[] = {0x02, 0x02, 0x00, 0x00};
    const uint8_t protocole[] = {0x02, 0x02, 0x02, 0x00};
    const uint8_t timer[] = {0x09, 0x04, 0x3A, 0x00, 0x58, 0x04};
    const uint8_t gain[] = {0x09, 0x04, 0x68, 0x01, 0x01, 0xD1};
    const uint8_t detection[] = {0x04, 0x02, 0x26, 0x07};
    uint8_t reponse[32];

    /* Repartir du repos a chaque lecture : pas besoin de HALT/WUPA. */
    uart_flush_input(PORT_RFID);
    if (envoyer_commande(arret_champ, sizeof(arret_champ), reponse) != 0) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (envoyer_commande(protocole, sizeof(protocole), reponse) != 0) return false;
    if (envoyer_commande(timer, sizeof(timer), reponse) != 0) return false;
    if (envoyer_commande(gain, sizeof(gain), reponse) != 0) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (envoyer_commande(detection, sizeof(detection), reponse) != 5) return false;
    if (reponse[2] != 0x28 || reponse[3] != 0 || reponse[4] != 0) return false;

    uint8_t uid[10];
    int longueur = lire_uid(uid);
    if (longueur == 0) return false;
    for (int indice = 0; indice < longueur; indice++) {
        snprintf(texte_uid + indice * 2, 3, "%02X", (unsigned int)uid[indice]);
    }
    return true;
}

/* Actualise la presence du badge en arriere-plan toutes les 200 ms. */
static void lecture_badge(void *argument)
{
    (void)argument;
    bool premiere_lecture = true;
    while (true) {
        char texte_uid[21];
        if (rechercher_badge(texte_uid)) {
            publier_badge(texte_uid);
        } else if (!cat_identification_get_snapshot().badge_present) {
            publier_badge(NULL);
            if (premiere_lecture) ESP_LOGI("RFID", "Badge absent");
        }
        premiere_lecture = false;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* Configure la liaison serie, synchronise le lecteur et lance la lecture. */
esp_err_t cat_identification_init(void)
{
    if (uart_initialise) return ESP_ERR_INVALID_STATE;
    publier_badge(NULL);
    const uart_config_t configuration = {
        .baud_rate = 57600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(PORT_RFID, &configuration) != ESP_OK) return ESP_FAIL;
    if (uart_set_pin(PORT_RFID, CROQCHAT_GPIO_RFID_TX, CROQCHAT_GPIO_RFID_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return ESP_FAIL;
    if (uart_driver_install(PORT_RFID, 1024, 0, 0, NULL, 0) != ESP_OK) return ESP_FAIL;
    uart_initialise = true;

    /* ECHO peut necessiter jusqu'a 528 essais de resynchronisation. */
    uint8_t commande = 0x55;
    uint8_t reponse = 0;
    bool synchronise = false;
    uart_flush_input(PORT_RFID);
    for (int essai = 0; essai < 530; essai++) {
        uart_write_bytes(PORT_RFID, &commande, 1);
        int attente = 20;
        if (essai == 0) attente = 100;
        int recus = uart_read_bytes(PORT_RFID, &reponse, 1, pdMS_TO_TICKS(attente));
        if (recus == 1 && reponse == 0x55) {
            synchronise = true;
            break;
        }
        if (essai == 0) {
            uart_wait_tx_done(PORT_RFID, pdMS_TO_TICKS(100));
            uart_set_line_inverse(PORT_RFID, UART_SIGNAL_TXD_INV);
            esp_rom_delay_us(100);
            uart_set_line_inverse(PORT_RFID, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (!synchronise) {
        ESP_LOGW("RFID", "Aucune reponse ECHO");
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI("RFID", "Lecteur pret. Presenter une carte.");
    if (xTaskCreate(lecture_badge, "rfid", 3072, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

/* Copie le badge et rejette une lecture trop ancienne. */
cat_identification_snapshot_t cat_identification_get_snapshot(void)
{
    portENTER_CRITICAL(&verrou_badge);
    cat_identification_snapshot_t lecture = badge;
    int64_t age = esp_timer_get_time() - derniere_lecture_us;
    portEXIT_CRITICAL(&verrou_badge);
    /* Meme si la tache RFID se bloque, une ancienne lecture ne garde pas le moteur actif. */
    if (age < 0 || age >= DELAI_PERTE_BADGE_US) {
        lecture.badge_present = false;
        lecture.identifiant_badge[0] = '\0';
    }
    return lecture;
}
