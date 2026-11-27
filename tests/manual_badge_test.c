#include "manual_badge.h"
#include "cat_identification.h"
#include "croqchat_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cat_identification_snapshot_t lecture;
static bool moteur;
static unsigned commandes;
static bool tare, refuser_tare;
static uint32_t nombre_tares;
bool weight_manager_is_busy(void) { return tare; }
uint32_t weight_manager_tare_count(void) { return nombre_tares; }
void weight_manager_cancel_operation(void) { tare = false; }
bool weight_manager_demander_tare(void)
{ if (tare || refuser_tare || moteur) return false; tare = true; return true; }
static void terminer_tare(void)
{
    if (tare) { tare = false; ++nombre_tares; }
    manual_badge_update(true, false, true);
}
cat_identification_snapshot_t cat_identification_get_snapshot(void) { return lecture; }
void motor_controller_update(bool tourne, uint32_t vitesse)
{ assert(vitesse == CROQCHAT_VITESSE_BADGE); moteur = tourne; ++commandes; }
void motor_controller_stop(void) { moteur = false; ++commandes; }
static void badge(const char *uid)
{
    lecture.badge_present = uid != NULL;
    snprintf(lecture.identifiant_badge, sizeof(lecture.identifiant_badge), "%s", uid ? uid : "");
}
int main(void)
{
    assert(!manual_badge_init(false));
    badge("04ABCDEF"); manual_badge_update(true,false, true); assert(!moteur);
    assert(manual_badge_init(true));
    manual_badge_update(true,false, true); assert(!moteur && manual_badge_is_taring());
    manual_badge_update(true,false, true); assert(!moteur); /* Aucun demarrage pendant la tare. */
    terminer_tare(); assert(moteur && manual_badge_is_active());
    manual_badge_update(true,false, true); assert(moteur);
    badge(NULL); manual_badge_update(true,false, true); assert(!moteur && !manual_badge_is_active());
    badge("04ABCDE0"); manual_badge_update(true,false, true); terminer_tare(); assert(moteur);
    badge("04ABCDEF00"); manual_badge_update(true,false, true); assert(moteur); /* Tout UID accepte. */
    badge("04ABCDEF"); manual_badge_update(true,false, true); terminer_tare(); assert(moteur);
    manual_badge_update(true,true, true); assert(!moteur); /* STOP prioritaire. */
    manual_badge_update(true,false, true); assert(!moteur); /* Pas de redemarrage badge tenu. */
    badge(NULL); manual_badge_update(true,false, true);
    badge("04ABCDEF"); manual_badge_update(true,false, true); terminer_tare(); assert(moteur);
    manual_badge_update(false,false, true); assert(!moteur); /* Interdiction : tare/defaut. */
    /* Le moteur d'une distribution ne doit pas etre coupe par le mode badge inactif. */
    moteur=true; unsigned avant=commandes;
    manual_badge_update(false,false, true); assert(moteur && commandes==avant);
    badge(NULL); manual_badge_update(false,false, true); assert(moteur && commandes==avant);
    moteur=false; badge("04ABCDEF"); manual_badge_update(true,false, true); terminer_tare(); assert(moteur);
    badge("00112233"); manual_badge_update(true,false, true); assert(moteur);
    assert(manual_badge_init(true));
    badge("0123456789ABCD"); manual_badge_update(true,false, true); terminer_tare(); assert(moteur);
    assert(manual_badge_init(true)); assert(!moteur);
    badge("0123456789ABCDEF0123"); manual_badge_update(true,false, true); terminer_tare(); assert(moteur);
    assert(!manual_badge_init(false)); assert(!moteur);
    assert(manual_badge_init(true));
    badge("04ABCDEF"); manual_badge_update(true,false, true); assert(tare && !moteur);
    badge(NULL); manual_badge_update(true,false, true); assert(!tare && !moteur);
    badge("04ABCDEF"); manual_badge_update(true,false, true); assert(tare);
    manual_badge_update(true,true, true); assert(!tare && !moteur);
    manual_badge_update(true,false, true); assert(!tare && !moteur);
    badge(NULL); manual_badge_update(true,false, true);
    badge("04ABCDEF"); manual_badge_update(true,false, true); assert(tare);
    tare=false; /* Timeout : operation finie sans nouvelle tare reussie. */
    manual_badge_update(true,false, true); assert(!moteur && !manual_badge_is_active());
    manual_badge_update(true,false, true); assert(!tare && !moteur);
    badge(NULL); manual_badge_update(true,false, true);
    refuser_tare=true; badge("04ABCDEF"); manual_badge_update(true,false, true);
    assert(!moteur && !tare);
    puts("Manual badge: any-card/disabled/removed/STOP/ownership PASS (no motor)");
}
