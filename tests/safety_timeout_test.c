/* Simule le temps, les mesures et le moteur pour contrôler les délais et le verrouillage des défauts. */
/* Vrai SafetyManager, horloge et sorties simulees : aucun moteur physique. */
#include "safety_manager.h"
#include "dispenser_controller.h"
#include "weight_manager.h"
#include "croqchat_config.h"
#include "cat_identification.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static int64_t now;
cat_identification_snapshot_t cat_identification_get_snapshot(void) { return (cat_identification_snapshot_t){0}; }
static bool moteur, badge, preparation_badge;
static bool sensor=true;
static float grammes;
static dispenser_state_t etat;
int64_t esp_timer_get_time(void) { return now; }
bool motor_controller_is_running(void) { return moteur; }
void motor_controller_stop(void) { moteur=false; }
bool manual_badge_is_active(void) { return badge || preparation_badge; }
bool manual_badge_is_taring(void) { return preparation_badge; }
bool dispenser_controller_is_busy(void)
{ return etat==DISPENSER_STARTING || etat==DISPENSER_FEEDING || etat==DISPENSER_SETTLING; }
dispenser_state_t dispenser_controller_get_state(void) { return etat; }
void dispenser_controller_stop(void) { moteur=false; etat=DISPENSER_IDLE; }
bool weight_manager_is_busy(void) { return preparation_badge || etat==DISPENSER_STARTING; }
weight_snapshot_t weight_manager_get_snapshot(void)
{ return (weight_snapshot_t){.sensor_ready=sensor,.calibrated=true,.tared=true,.grams=grammes}; }
static const int64_t limite=(int64_t)CROQCHAT_TIMEOUT_MOTEUR_MS*1000;
static void reset(void)
{
    moteur=badge=preparation_badge=false; now=0; grammes=0; etat=DISPENSER_IDLE;
    assert(safety_manager_init()==ESP_OK);
}
int main(void)
{
    reset();
    etat=DISPENSER_STARTING;
    safety_manager_tick(); now=limite*2; safety_manager_tick();
    assert(safety_manager_get_fault()==SAFETY_FAULT_NONE); /* Tare hors timeout moteur. */
    etat=DISPENSER_FEEDING; moteur=true; safety_manager_tick();
    now+=limite-1; grammes+=2; safety_manager_tick();
    assert(moteur && safety_manager_get_fault()==SAFETY_FAULT_NONE);
    etat=DISPENSER_SETTLING; moteur=false; ++now; safety_manager_tick();
    assert(!moteur && !dispenser_controller_is_busy());
    assert(safety_manager_get_fault()==SAFETY_FAULT_TIMEOUT);
    assert(!safety_manager_can_feed(60));
    moteur=true; safety_manager_tick(); assert(!moteur); /* Pas de redemarrage sur defaut. */

    reset(); preparation_badge=true; safety_manager_tick();
    now=limite*2; safety_manager_tick(); assert(safety_manager_get_fault()==SAFETY_FAULT_NONE);
    preparation_badge=false; badge=true; moteur=true; safety_manager_tick();
    now+=limite; grammes+=2; safety_manager_tick();
    assert(!moteur && safety_manager_get_fault()==SAFETY_FAULT_TIMEOUT);

    reset(); moteur=true; safety_manager_tick(); /* Test moteur direct egalement protege. */
    now=limite; grammes+=2; safety_manager_tick(); assert(!moteur && safety_manager_get_fault()==SAFETY_FAULT_TIMEOUT);

    reset(); etat=DISPENSER_FEEDING; moteur=true; safety_manager_tick();
    now=limite/2; safety_manager_request_stop(); safety_manager_tick();
    now=limite*2; etat=DISPENSER_FEEDING; moteur=true; safety_manager_tick();
    now+=limite-1; grammes+=2; safety_manager_tick(); assert(moteur && safety_manager_get_fault()==SAFETY_FAULT_NONE);
    ++now; safety_manager_tick(); assert(!moteur && safety_manager_get_fault()==SAFETY_FAULT_TIMEOUT);
    reset(); sensor=false; etat=DISPENSER_FEEDING; moteur=true;
    safety_manager_tick();assert(!moteur && safety_manager_get_fault()==SAFETY_FAULT_SENSOR);
    assert(safety_manager_reset_fault()!=ESP_OK);
    sensor=true;assert(safety_manager_reset_fault()==ESP_OK);
    assert(safety_manager_get_fault()==SAFETY_FAULT_NONE);
    reset();
    badge=true; moteur=true; safety_manager_tick();
    now=9999999; grammes=0.2f; safety_manager_tick();
    assert(moteur && safety_manager_get_fault()==SAFETY_FAULT_NONE);
    ++now; safety_manager_tick();
    assert(!moteur && safety_manager_get_fault()==SAFETY_FAULT_NO_FLOW);

    reset(); etat=DISPENSER_SETTLING; moteur=true; safety_manager_tick();
    now=9000000; grammes=1.0f; safety_manager_tick();
    moteur=false; now=18999999; safety_manager_tick();
    assert(safety_manager_get_fault()==SAFETY_FAULT_NONE);
    ++now; safety_manager_tick();
    assert(!dispenser_controller_is_busy() && safety_manager_get_fault()==SAFETY_FAULT_NO_FLOW);
    puts("Motor timeout: 30s/frozen weight/fine pauses/badge/test/latched fault/new session PASS");
}
