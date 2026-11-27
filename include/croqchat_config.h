#ifndef CROQCHAT_CONFIG_H
#define CROQCHAT_CONFIG_H

/* Configuration commune, sans dependance rani pilote materiel. */
#define CROQCHAT_DEVICE_NAME "CroqChat-ESP32"
#define CROQCHAT_SERVICE_UUID "32a44c10-fdd0-4b30-bb4b-5a8900000001"
#define CROQCHAT_RX_UUID "32a44c10-fdd0-4b30-bb4b-5a8900000002"
#define CROQCHAT_TX_UUID "32a44c10-fdd0-4b30-bb4b-5a8900000003"
#define CROQCHAT_COMMAND_MAX 256
#define CROQCHAT_FRAGMENT_MAX 512
#define CROQCHAT_NOTIFY_MAX 20
#define CROQCHAT_RX_QUEUE_DEPTH 12
#define CROQCHAT_TX_QUEUE_DEPTH 24
#define CROQCHAT_TX_LINE_MAX 192
#define CROQCHAT_COMMAND_QUEUE_DEPTH 8
#define CROQCHAT_MEAL_COUNT 6
#define CROQCHAT_MAX_MASS_GRAMS 1000.0f
/* Capteur de poids : HX711. */
#define CROQCHAT_GPIO_HX711_DOUT 34
#define CROQCHAT_GPIO_HX711_SCK 25

/* Moteur de distribution : entrees du pilote. */
#define CROQCHAT_GPIO_MOTOR_IN1 26
#define CROQCHAT_GPIO_MOTOR_IN2 27
#define CROQCHAT_GPIO_MOTOR_IN3 14
#define CROQCHAT_GPIO_MOTOR_IN4 13

/* Lecteur RFID : liaison UART. */
#define CROQCHAT_GPIO_RFID_RX 16
#define CROQCHAT_GPIO_RFID_TX 17
#define CROQCHAT_VITESSE_BADGE 500U
/* RAPPEL / A REGLER apres essais : duree maximale d'une distribution (pauses incluses).
 * Valeur provisoire : 30 secondes. La tare a son propre timeout de 10 secondes. */
#define CROQCHAT_TIMEOUT_MOTEUR_MS 30000U
#if CROQCHAT_TIMEOUT_MOTEUR_MS == 0
#error "Le timeout moteur doit etre strictement positif"
#endif

#endif
