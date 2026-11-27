/* Vérifie que la distribution attend sa propre tare et s’arrête si elle échoue. */
#include "dispenser_controller.h"
#include "weight_manager.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static weight_snapshot_t poids = {.grams=45,.sensor_ready=true,.calibrated=true,.tared=true};
static bool moteur, tare, refus;
static uint32_t numero_tare, cadence_moteur;
static unsigned demandes_tare;
static int64_t now;
weight_snapshot_t weight_manager_get_snapshot(void) { return poids; }
bool weight_manager_is_busy(void) { return tare; }
uint32_t weight_manager_tare_count(void) { return numero_tare; }
bool weight_manager_demander_tare(void)
{ if(tare || refus || moteur)return false; tare=true; ++demandes_tare; return true; }
void weight_manager_cancel_operation(void) { tare=false; }
bool motor_controller_is_running(void) { return moteur; }
void motor_controller_stop(void) { moteur=false; }
void motor_controller_update(bool tourne, uint32_t vitesse) { cadence_moteur=vitesse; moteur=tourne; }
int64_t esp_timer_get_time(void) { return now; }
static void finir_tare(void) { tare=false; ++numero_tare; poids.grams=0; }
int main(void)
{
    dispenser_controller_init();
    dispenser_controller_request(60);
    assert(tare && !moteur && dispenser_controller_is_busy());
    assert(dispenser_controller_get_state()==DISPENSER_STARTING);
    poids.grams=100; /* Ancienne valeur : ne doit pas conclure la distribution. */
    dispenser_controller_tick(); assert(!moteur && dispenser_controller_is_busy());
    unsigned avant=demandes_tare;
    dispenser_controller_request(30); assert(demandes_tare==avant);
    finir_tare(); dispenser_controller_tick(); assert(moteur); assert(cadence_moteur==500);
    poids.grams=45; dispenser_controller_tick(); assert(moteur && cadence_moteur==250);
    poids.grams=54.9f; dispenser_controller_tick(); assert(moteur && cadence_moteur==250);
    poids.grams=55; dispenser_controller_tick(); assert(!moteur);
    assert(dispenser_controller_get_state()==DISPENSER_SETTLING);
    now+=200000; dispenser_controller_tick(); assert(moteur && cadence_moteur==500);
    now+=100000; dispenser_controller_tick(); assert(!moteur);
    poids.grams=60; dispenser_controller_tick(); assert(!moteur);
    assert(dispenser_controller_get_state()==DISPENSER_DONE && dispenser_controller_last_mass()==60);
    dispenser_controller_request(20); assert(tare && demandes_tare==avant+1 && !moteur);
    dispenser_controller_stop(); assert(!tare && !moteur && !dispenser_controller_is_busy());
    dispenser_controller_tick(); assert(!moteur); /* STOP pendant tare : aucun redemarrage. */
    dispenser_controller_request(20); tare=false; /* Timeout / annulation sans nouvelle tare. */
    dispenser_controller_tick(); assert(!moteur && dispenser_controller_get_state()==DISPENSER_FAULT);
    refus=true; dispenser_controller_request(20);
    assert(!moteur && dispenser_controller_get_state()==DISPENSER_FAULT);
    refus=false; dispenser_controller_request(20); finir_tare(); dispenser_controller_tick(); assert(moteur);
    dispenser_controller_stop();
    avant=demandes_tare;
    dispenser_controller_request(NAN); assert(!moteur && demandes_tare==avant);
    puts("Dispenser: tare before every portion/wait/STOP/failure PASS");
}
