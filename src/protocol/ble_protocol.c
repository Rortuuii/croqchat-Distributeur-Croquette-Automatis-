/* Reconstruction des lignes recues et verification des commandes. */
#include "ble_protocol.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Correspondance entre les noms de commandes du protocole et leurs identifiants internes. */
static const char *const noms[] = {
    "GET_STATUS", "GET_MEALS", "SET_MEAL", "DELETE_MEAL", "MANUAL_FEED",
    "STOP", "TARE", "CALIBRATE", "RESET_FAULT", "SET_TIME"
};
/* Renvoie le nom texte du type de commande. */
const char *protocol_command_name(protocol_command_type_t type)
{
    return (unsigned)type < sizeof(noms) / sizeof(noms[0]) ? noms[type] : "UNKNOWN";
}
/* Vide la ligne en cours et reinitialise la reception. */
void protocol_stream_reset(protocol_stream_t *flux)
{
    memset(flux, 0, sizeof(*flux));
}
/* Assemble les octets jusqu au saut de ligne et rejette les trames invalides. */
/* Assemble une ligne à partir de fragments et écarte une trame invalide jusqu’à la prochaine fin de ligne. */
void protocol_stream_feed(protocol_stream_t *flux, const uint8_t *donnees, size_t longueur,
                          protocol_line_cb_t traiter_ligne, protocol_error_cb_t traiter_erreur, void *contexte)
{
    for (size_t indice = 0; indice < longueur; ++indice) {
        uint8_t caractere = donnees[indice];
        if (flux->abandon_en_cours) {
            if (caractere == '\n') flux->abandon_en_cours = false;
            continue;
        }
        if (caractere == '\n') {
            if (flux->longueur && flux->ligne[flux->longueur - 1] == '\r') --flux->longueur;
            flux->ligne[flux->longueur] = '\0';
            if (flux->longueur) traiter_ligne(flux->ligne, contexte);
            flux->longueur = 0;
            continue;
        }
        const char *erreur = NULL;
        if ((caractere < 32 && caractere != '\r') || caractere > 126 ||
            (flux->longueur && flux->ligne[flux->longueur - 1] == '\r')) erreur = "BAD_ASCII";
        else if (flux->longueur >= CROQCHAT_COMMAND_MAX) erreur = "RX_OVERFLOW";
        if (erreur) {
            flux->longueur = 0;
            flux->abandon_en_cours = true;
            traiter_erreur(erreur, contexte);
        } else {
            flux->ligne[flux->longueur++] = (char)caractere;
        }
    }
}
/* Lit un entier et verifie ses limites. */
/* Convertit un champ entier, vérifie ses bornes et rejette les caractères supplémentaires. */
static bool integer(const char *valeur, int64_t minimum, int64_t maximum, int64_t *resultat)
{
    if (!*valeur || (*valeur != '-' && (*valeur < '0' || *valeur > '9'))) return false;
    const char *chiffres = *valeur == '-' ? valeur + 1 : valeur;
    if (!*chiffres) return false;
    for (const char *curseur = chiffres; *curseur; ++curseur) if (*curseur < '0' || *curseur > '9') return false;
    errno = 0;
    char *fin;
    long long nombre = strtoll(valeur, &fin, 10);
    if (errno || *fin || nombre < minimum || nombre > maximum) return false;
    *resultat = (int64_t)nombre;
    return true;
}
/* Lit une masse positive et verifie sa limite maximale. */
/* Accepte uniquement une masse finie et positive dans la plage du distributeur. */
static bool mass(const char *valeur, float *resultat)
{
    if (*valeur < '0' || *valeur > '9') return false;
    errno = 0;
    char *fin;
    float nombre = strtof(valeur, &fin);
    if (errno || *fin || !isfinite(nombre) || nombre <= 0 || nombre > CROQCHAT_MAX_MASS_GRAMS) return false;
    *resultat = nombre;
    return true;
}
/* Verifie le nom et les parametres avant de construire une commande. */
/* Valide le nom de la commande, les champs attendus et leurs valeurs avant exécution. */
bool protocol_parse(const char *ligne, protocol_command_t *commande, const char **code_erreur)
{
    *code_erreur = "BAD_FORMAT";
    if (!ligne || strlen(ligne) > CROQCHAT_COMMAND_MAX) return false;
    char tampon[CROQCHAT_COMMAND_MAX + 1];
    strcpy(tampon, ligne);
    memset(commande, 0, sizeof(*commande));
    char *champ = strchr(tampon, ';');
    if (champ) *champ++ = '\0';
    bool connue = false;
    for (unsigned indice = 0; indice < sizeof(noms) / sizeof(noms[0]); ++indice) {
        if (strcmp(tampon, noms[indice]) == 0) { commande->type = (protocol_command_type_t)indice; connue = true; break; }
    }
    if (!connue) { *code_erreur = "UNKNOWN_COMMAND"; return false; }
    enum { ID = 1, TIME = 2, MASS = 4, ENABLE = 8, REF = 16, EPOCH = 32, OFFSET = 64 };
    unsigned requis = 0;
    switch (commande->type) {
        case CMD_SET_MEAL: requis = ID | TIME | MASS | ENABLE; break;
        case CMD_DELETE_MEAL: requis = ID; break;
        case CMD_MANUAL_FEED: requis = MASS; break;
        case CMD_CALIBRATE: requis = REF; break;
        case CMD_SET_TIME: requis = EPOCH | OFFSET; break;
        default: break;
    }
    unsigned vus = 0;
    while (champ) {
        char *suivant = strchr(champ, ';');
        if (suivant) *suivant++ = '\0';
        char *valeur = strchr(champ, '=');
        if (!valeur || valeur == champ || !valeur[1]) return false;
        *valeur++ = '\0';
        unsigned masque = 0;
        if (!strcmp(champ, "id")) masque = ID;
        else if (!strcmp(champ, "time")) masque = TIME;
        else if (!strcmp(champ, "mass")) masque = MASS;
        else if (!strcmp(champ, "enable")) masque = ENABLE;
        else if (!strcmp(champ, "mass_ref")) masque = REF;
        else if (!strcmp(champ, "epoch")) masque = EPOCH;
        else if (!strcmp(champ, "offset")) masque = OFFSET;
        if (!masque || !(requis & masque)) { *code_erreur = "UNKNOWN_PARAM"; return false; }
        if (vus & masque) { *code_erreur = "DUPLICATE_PARAM"; return false; }
        vus |= masque;
        int64_t nombre;
        if (masque == ID) {
            if (!integer(valeur, 1, CROQCHAT_MEAL_COUNT, &nombre)) { *code_erreur = "INVALID_ID"; return false; }
            commande->identifiant_repas = (uint8_t)nombre;
        } else if (masque == TIME) {
            if (strlen(valeur) != 5 || valeur[2] != ':' ||
                valeur[0] < '0' || valeur[0] > '9' || valeur[1] < '0' || valeur[1] > '9' ||
                valeur[3] < '0' || valeur[3] > '9' || valeur[4] < '0' || valeur[4] > '9') { *code_erreur = "INVALID_TIME"; return false; }
            valeur[2] = '\0';
            if (!integer(valeur, 0, 23, &nombre)) { *code_erreur = "INVALID_TIME"; return false; }
            commande->heure = (uint8_t)nombre;
            if (!integer(valeur + 3, 0, 59, &nombre)) { *code_erreur = "INVALID_TIME"; return false; }
            commande->minute = (uint8_t)nombre;
        } else if (masque == MASS || masque == REF) {
            if (!mass(valeur, &commande->grammes)) { *code_erreur = "INVALID_MASS"; return false; }
        } else if (masque == ENABLE) {
            if (strcmp(valeur, "0") && strcmp(valeur, "1")) { *code_erreur = "INVALID_ENABLE"; return false; }
            commande->active = *valeur == '1';
        } else if (masque == EPOCH) {
            if (!integer(valeur, 1, INT64_C(253402300799), &commande->horodatage)) { *code_erreur = "INVALID_EPOCH"; return false; }
        } else if (masque == OFFSET) {
            if (!integer(valeur, -840, 840, &nombre)) { *code_erreur = "INVALID_OFFSET"; return false; }
            commande->decalage_minutes = (int16_t)nombre;
        }
        champ = suivant;
    }
    if (vus != requis) { *code_erreur = "MISSING_PARAM"; return false; }
    if (commande->type == CMD_SET_MEAL && (commande->grammes < 1 || commande->grammes > 200)) {
        *code_erreur = "INVALID_MEAL";
        return false;
    }
    *code_erreur = NULL;
    return true;
}
