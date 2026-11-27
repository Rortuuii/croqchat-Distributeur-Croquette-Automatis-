/* Reconstruction des lignes recues et verification des commandes. */
#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "croqchat_config.h"

typedef enum {
    CMD_GET_STATUS, CMD_GET_MEALS, CMD_SET_MEAL, CMD_DELETE_MEAL,
    CMD_MANUAL_FEED, CMD_STOP, CMD_TARE, CMD_CALIBRATE, CMD_RESET_FAULT, CMD_SET_TIME
} protocol_command_type_t;
typedef struct {
    protocol_command_type_t type;
    uint8_t identifiant_repas;
    uint8_t heure;
    uint8_t minute;
    float grammes;
    bool active;
    int64_t horodatage;
    int16_t decalage_minutes;
} protocol_command_t;
typedef struct {
    char ligne[CROQCHAT_COMMAND_MAX + 1];
    size_t longueur;
    bool abandon_en_cours;
} protocol_stream_t;
typedef void (*protocol_line_cb_t)(const char *ligne, void *contexte);
typedef void (*protocol_error_cb_t)(const char *code, void *contexte);
void protocol_stream_reset(protocol_stream_t *flux);
void protocol_stream_feed(protocol_stream_t *flux, const uint8_t *donnees, size_t longueur,
                          protocol_line_cb_t traiter_ligne, protocol_error_cb_t traiter_erreur, void *contexte);
bool protocol_parse(const char *ligne, protocol_command_t *commande, const char **code_erreur);
const char *protocol_command_name(protocol_command_type_t type);
#endif
