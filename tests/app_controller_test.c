/* Vérifie le coordinateur avec des périphériques simulés et les priorités entre demandes moteur. */
/* Vrai coordinateur + vrai controle badge, autres blocs simules. */
#include "app_controller.h"
#include "manual_badge.h"
#include "cat_identification.h"
#include "meal_scheduler.h"
#include "weight_manager.h"
#include "safety_manager.h"
#include "dispenser_controller.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cat_identification_snapshot_t lecture;
static bool moteur, distribution, tare, stop, echeance, fin;
static unsigned demandes, reservations;
static unsigned ble_ticks, poids_ticks, planning_ticks, rfid_inits;
static bool capacite_balance, capacite_scheduler;
static bool tare_badge;
static uint32_t nombre_tares;
static dispenser_state_t etat;
static safety_fault_t defaut;
esp_err_t storage_init(void) { return ESP_OK; }
void weight_manager_init(void) {}
void weight_manager_update(void)
{ ++poids_ticks; if(tare_badge) { tare_badge=false; ++nombre_tares; } }
bool weight_manager_is_busy(void) { return tare || tare_badge; }
uint32_t weight_manager_tare_count(void) { return nombre_tares; }
void weight_manager_cancel_operation(void) { tare_badge=false; }
bool weight_manager_demander_tare(void)
{ if(tare || tare_badge || moteur)return false; tare_badge=true; return true; }
weight_snapshot_t weight_manager_get_snapshot(void)
{ return (weight_snapshot_t){.sensor_ready=true,.calibrated=true,.tared=true}; }
void dispenser_controller_init(void) { etat=DISPENSER_IDLE; }
bool dispenser_controller_is_busy(void) { return distribution; }
dispenser_state_t dispenser_controller_get_state(void) { return etat; }
void dispenser_controller_request(float grams)
{ assert(grams==60 && !distribution && !moteur); distribution=true; etat=DISPENSER_STARTING; ++demandes; }
void dispenser_controller_stop(void) { distribution=moteur=false; etat=DISPENSER_IDLE; }
void dispenser_controller_tick(void)
{
    if (fin) { fin=false; distribution=moteur=false; etat=DISPENSER_DONE; }
    else if (distribution) { moteur=true; etat=DISPENSER_FEEDING; }
}
esp_err_t safety_manager_init(void) { return ESP_OK; }
void safety_manager_tick(void) {}
safety_fault_t safety_manager_get_fault(void) { return defaut; }
bool safety_manager_can_feed(float grams)
{ (void)grams; return !moteur && !distribution && !tare && defaut==SAFETY_FAULT_NONE; }
esp_err_t meal_scheduler_init(void) { return ESP_OK; }
void meal_scheduler_update(void) { ++planning_ticks; }
bool meal_scheduler_take_due(meal_t *meal)
{ if(!echeance)return false; echeance=false; *meal=(meal_t){.identifiant=1,.grammes=60}; return true; }
esp_err_t meal_scheduler_claim(uint8_t id) { assert(id==1); ++reservations; return ESP_OK; }
void meal_scheduler_report(bool success) { (void)success; }
esp_err_t ble_manager_start(void) { return ESP_OK; }
void ble_manager_set_features(bool balance, bool scheduler)
{ capacite_balance=balance; capacite_scheduler=scheduler; }
bool ble_manager_tick(void)
{ ++ble_ticks; if(!stop)return false; stop=false; dispenser_controller_stop(); return true; }
esp_err_t cat_identification_init(void) { ++rfid_inits; return ESP_OK; }
cat_identification_snapshot_t cat_identification_get_snapshot(void) { return lecture; }
void motor_controller_update(bool tourne, uint32_t vitesse) { assert(vitesse==500); moteur=tourne; }
void motor_controller_stop(void) { moteur=false; }
TickType_t xTaskGetTickCount(void) { return 0; }
static void badge(bool present)
{ lecture.badge_present=present; strcpy(lecture.identifiant_badge,present ? "04ABCDEF" : ""); }
int main(void)
{
    app_options_t options = {.activer_ble=true,.activer_balance=true,.activer_scheduler=true,
        .activer_rfid=true,.activer_badge_manuel=true,.afficher_balance=true};
    app_controller_init(&options);
    assert(manual_badge_init(true));
    badge(true); app_controller_update(); assert(!moteur && manual_badge_is_taring());
    app_controller_update(); assert(moteur && manual_badge_is_active());
    echeance=true; app_controller_update(); assert(demandes==0 && reservations==0 && moteur);
    badge(false); app_controller_update(); assert(!moteur);
    /* Meme iteration : le repas a priorite sur un nouveau badge. */
    badge(true); echeance=true; app_controller_update();
    assert(distribution && demandes==1 && reservations==1 && !manual_badge_is_active());
    app_controller_update(); assert(moteur && distribution && !manual_badge_is_active());
    /* Le mode badge reprend uniquement apres la fin du distributeur. */
    fin=true; app_controller_update(); assert(!distribution && !moteur && manual_badge_is_taring());
    app_controller_update(); assert(moteur && manual_badge_is_active());
    stop=true; app_controller_update(); assert(!moteur);
    app_controller_update(); assert(!moteur);
    badge(false); app_controller_update(); badge(true); app_controller_update();
    assert(!moteur && manual_badge_is_taring()); app_controller_update(); assert(moteur);
    tare=true; app_controller_update(); assert(!moteur); tare=false;
    app_controller_update(); assert(!moteur && manual_badge_is_taring());
    app_controller_update(); assert(moteur);
    defaut=SAFETY_FAULT_SENSOR; app_controller_update(); assert(!moteur);
    /* Un moteur FB03 deja en cours est conserve quand le badge est absent. */
    defaut=SAFETY_FAULT_NONE; badge(false); distribution=true;
    app_controller_update(); assert(moteur && distribution && !manual_badge_is_active());
    stop=true; echeance=true; app_controller_update(); assert(!moteur && demandes==1);
    /* Les drapeaux false coupent effectivement les appels correspondants. */
    unsigned ble_avant=ble_ticks, poids_avant=poids_ticks, planning_avant=planning_ticks;
    unsigned rfid_avant=rfid_inits;
    options=(app_options_t){0};
    app_controller_init(&options); badge(true); app_controller_update();
    assert(ble_ticks==ble_avant && poids_ticks==poids_avant && planning_ticks==planning_avant);
    assert(rfid_inits==rfid_avant && !moteur && !capacite_balance && !capacite_scheduler);
    /* Pas de repas sans balance ni de commande par badge sans lecteur. */
    options.activer_ble=true; options.activer_scheduler=true; options.activer_badge_manuel=true;
    app_controller_init(&options); app_controller_update();
    assert(ble_ticks==ble_avant+1 && poids_ticks==poids_avant && planning_ticks==planning_avant);
    assert(!capacite_scheduler && !moteur && rfid_inits==rfid_avant);
    options = (app_options_t){.activer_balance=true,.activer_rfid=true,
        .activer_badge_manuel=true,.badge_sans_tare=true};
    app_controller_init(&options);
    badge(true); app_controller_update(); assert(moteur && !tare_badge);
    badge(false); app_controller_update(); assert(!moteur);
    puts("Coordinator: badge/scheduler/dispenser/STOP/fault priorities PASS");
}
