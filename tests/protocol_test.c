/* Tests portables : compiler avec un compilateur C natif, voir docs/tests-ble.md.
 * Aucune dependance ESP-IDF, aucun acces au materiel. */
#include "ble_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned nombre_lignes, nombre_erreurs;
static char derniere_ligne[CROQCHAT_COMMAND_MAX + 1];
static char derniere_erreur[32];
static void line_cb(const char *ligne, void *argument)
{
    (void)argument;
    ++nombre_lignes;
    strcpy(derniere_ligne, ligne);
}
static void error_cb(const char *code, void *argument)
{
    (void)argument;
    ++nombre_erreurs;
    strcpy(derniere_erreur, code);
}
static void feed(protocol_stream_t *flux, const char *texte)
{
    protocol_stream_feed(flux, (const uint8_t *)texte, strlen(texte), line_cb, error_cb, NULL);
}
static void valid(const char *ligne, protocol_command_type_t type)
{
    protocol_command_t commande;
    const char *erreur;
    assert(protocol_parse(ligne, &commande, &erreur));
    assert(commande.type == type && erreur == NULL);
}
static void invalid(const char *ligne, const char *attendu)
{
    protocol_command_t commande;
    const char *erreur;
    assert(!protocol_parse(ligne, &commande, &erreur));
    assert(strcmp(erreur, attendu) == 0);
}
int main(void)
{
    protocol_stream_t flux;
    protocol_stream_reset(&flux);
    feed(&flux, "MANUAL_");
    feed(&flux, "FEED;mass=20");
    assert(nombre_lignes == 0);
    feed(&flux, "\n");
    assert(nombre_lignes == 1 && strcmp(derniere_ligne, "MANUAL_FEED;mass=20") == 0);
    feed(&flux, "STOP\nTARE\nGET_STATUS\r");
    assert(nombre_lignes == 3);
    feed(&flux, "\n\n\r\n");
    assert(nombre_lignes == 4 && strcmp(derniere_ligne, "GET_STATUS") == 0);
    char grand_tampon[CROQCHAT_COMMAND_MAX + 2];
    memset(grand_tampon, 'X', sizeof(grand_tampon));
    protocol_stream_feed(&flux, (uint8_t *)grand_tampon, CROQCHAT_COMMAND_MAX, line_cb, error_cb, NULL);
    feed(&flux, "\n");
    assert(nombre_lignes == 5 && strlen(derniere_ligne) == CROQCHAT_COMMAND_MAX && nombre_erreurs == 0);
    protocol_stream_feed(&flux, (uint8_t *)grand_tampon, CROQCHAT_COMMAND_MAX + 1, line_cb, error_cb, NULL);
    feed(&flux, "suffix\nGET_STATUS\n");
    assert(nombre_erreurs == 1 && strcmp(derniere_erreur, "RX_OVERFLOW") == 0);
    assert(nombre_lignes == 6 && strcmp(derniere_ligne, "GET_STATUS") == 0);
    const uint8_t donnees_invalides[] = {'S', 0, 'T', '\n'};
    protocol_stream_feed(&flux, donnees_invalides, sizeof(donnees_invalides), line_cb, error_cb, NULL);
    assert(nombre_erreurs == 2 && strcmp(derniere_erreur, "BAD_ASCII") == 0 && nombre_lignes == 6);
    feed(&flux, "OLD_PARTIAL");
    protocol_stream_reset(&flux);
    feed(&flux, "GET_MEALS\n");
    assert(nombre_lignes == 7 && strcmp(derniere_ligne, "GET_MEALS") == 0);

    feed(&flux, "STOP\rBAD\nGET_STATUS\n");
    assert(nombre_erreurs == 3 && strcmp(derniere_erreur, "BAD_ASCII") == 0);
    assert(nombre_lignes == 8 && strcmp(derniere_ligne, "GET_STATUS") == 0);

    valid("GET_STATUS", CMD_GET_STATUS);
    valid("GET_MEALS", CMD_GET_MEALS);
    valid("SET_MEAL;id=1;time=08:00;mass=60;enable=1", CMD_SET_MEAL);
    valid("SET_MEAL;enable=0;mass=1;time=23:59;id=6", CMD_SET_MEAL);
    invalid("SET_MEAL;id=1;time=08:00;mass=0.1;enable=1", "INVALID_MEAL");
    invalid("SET_MEAL;id=1;time=08:00;mass=201;enable=1", "INVALID_MEAL");
    valid("DELETE_MEAL;id=1", CMD_DELETE_MEAL);
    valid("MANUAL_FEED;mass=20", CMD_MANUAL_FEED);
    valid("STOP", CMD_STOP);
    valid("TARE", CMD_TARE);
    valid("CALIBRATE;mass_ref=100", CMD_CALIBRATE);
    valid("RESET_FAULT", CMD_RESET_FAULT);
    valid("SET_TIME;epoch=2200000000;offset=-60", CMD_SET_TIME);
    protocol_command_t commande;
    const char *erreur;
    assert(protocol_parse("SET_TIME;epoch=2200000000;offset=-60", &commande, &erreur));
    assert(commande.horodatage == INT64_C(2200000000) && commande.decalage_minutes == -60);
    assert(protocol_parse("SET_MEAL;id=6;time=23:59;mass=60.5;enable=1", &commande, &erreur));
    assert(commande.identifiant_repas == 6 && commande.heure == 23 && commande.minute == 59);
    assert(commande.grammes == 60.5f && commande.active);
    invalid("MANUAL_FEED;mass=0", "INVALID_MASS");
    invalid("MANUAL_FEED;mass=nan", "INVALID_MASS");
    invalid("MANUAL_FEED;mass=inf", "INVALID_MASS");
    invalid("MANUAL_FEED;mass=-2", "INVALID_MASS");
    invalid("MANUAL_FEED;mass=1001", "INVALID_MASS");
    invalid("MANUAL_FEED;mass=20junk", "INVALID_MASS");
    invalid("MANUAL_FEED;mass=20;mass=30", "DUPLICATE_PARAM");
    invalid("MANUAL_FEED;mass=", "BAD_FORMAT");
    invalid("MANUAL_FEED", "MISSING_PARAM");
    invalid("GET_STATUS;x=1", "UNKNOWN_PARAM");
    invalid("GET_STATUS;", "BAD_FORMAT");
    invalid("BOGUS;x=abc", "UNKNOWN_COMMAND");
    invalid("DELETE_MEAL;id=7", "INVALID_ID");
    invalid("SET_MEAL;id=1;time=24:00;mass=60;enable=1", "INVALID_TIME");
    invalid("SET_MEAL;id=1;time=-0:00;mass=60;enable=1", "INVALID_TIME");
    invalid("SET_MEAL;id=1;time=08:00;mass=60;enable=2", "INVALID_ENABLE");
    invalid("SET_TIME;epoch=99999999999999999999999;offset=0", "INVALID_EPOCH");
    invalid("SET_TIME;epoch=1;offset=841", "INVALID_OFFSET");
    puts("Protocol tests: PASS");
    return 0;
}
