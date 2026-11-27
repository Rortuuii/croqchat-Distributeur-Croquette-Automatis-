/* Vrais scheduler/parseur/commandes/securite ; materiel et NVS simules. */
#include "meal_scheduler.h"
#include "ble_manager.h"
#include "weight_manager.h"
#include "dispenser_controller.h"
#include "safety_manager.h"
#include "cat_identification.h"
#include "freertos/queue.h"
#include "nvs.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int64_t now_us;
static unsigned writes, requests;
static unsigned char disk[1024], staged[1024];
static size_t disk_size, staged_size;
static bool fail_write, busy, session_active = true;
static weight_snapshot_t weight;
static ble_command_handler_t receive_command;
static char lines[32][CROQCHAT_TX_LINE_MAX];
static unsigned count;
int64_t esp_timer_get_time(void) { return now_us; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{ assert(!strcmp(name,"croq_repas")); (void)mode; *h=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h; assert(!strcmp(key,"planning"));
    if (!disk_size) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= disk_size); memcpy(out,disk,disk_size); *size=disk_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *in, size_t size)
{
    (void)h; (void)key; assert(size<=sizeof(staged));
    memcpy(staged,in,size); staged_size=size; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h; if (fail_write) return ESP_FAIL;
    memcpy(disk,staged,staged_size); disk_size=staged_size; ++writes; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
struct test_queue { unsigned capacity, count; size_t size; unsigned char data[8][sizeof(ble_command_event_t)]; };
QueueHandle_t xQueueCreate(unsigned n, size_t size)
{ QueueHandle_t q=calloc(1,sizeof(*q)); assert(q && n<=8 && size<=sizeof(q->data[0])); q->capacity=n; q->size=size; return q; }
void vQueueDelete(QueueHandle_t q) { free(q); }
int xQueueSend(QueueHandle_t q, const void *v, unsigned wait)
{ (void)wait; if(q->count==q->capacity)return 0; memcpy(q->data[q->count++],v,q->size); return 1; }
int xQueueOverwrite(QueueHandle_t q, const void *v)
{ q->count=0; return xQueueSend(q,v,0); }
int xQueueReceive(QueueHandle_t q, void *v, unsigned wait)
{ (void)wait; if(!q->count)return 0; memcpy(v,q->data[0],q->size); --q->count; memmove(q->data[0],q->data[1],q->count*q->size); return 1; }
esp_err_t ble_manager_init(ble_command_handler_t cb) { receive_command=cb; return ESP_OK; }
bool ble_manager_session_active(uint32_t session) { return session_active && session==1; }
esp_err_t ble_manager_send_line(uint32_t session, const char *line)
{ assert(session==1 && count<32); snprintf(lines[count++],sizeof(lines[0]),"%s",line); return ESP_OK; }
weight_snapshot_t weight_manager_get_snapshot(void) { return weight; }
bool weight_manager_is_busy(void) { return false; }
void weight_manager_cancel_operation(void) {}
bool weight_manager_demander_tare(void) { return true; }
bool weight_manager_demander_calibration(float mass) { (void)mass; return true; }
bool motor_controller_is_running(void) { return busy; }
bool manual_badge_is_active(void) { return false; }
bool manual_badge_is_taring(void) { return false; }
void motor_controller_stop(void) { busy=false; }
bool dispenser_controller_is_busy(void) { return busy; }
dispenser_state_t dispenser_controller_get_state(void) { return busy ? DISPENSER_FEEDING : DISPENSER_IDLE; }
void dispenser_controller_request(float mass) { assert(mass>0 && !busy); busy=true; ++requests; }
void dispenser_controller_stop(void) { busy=false; }
float dispenser_controller_last_mass(void) { return 42.0f; }
cat_identification_snapshot_t cat_identification_get_snapshot(void)
{ return (cat_identification_snapshot_t){0}; }
static bool command(const char *text)
{
    count=0;
    ble_command_event_t event={.session=1}; const char *error;
    assert(protocol_parse(text,&event.commande,&error));
    assert(receive_command(&event));
    return ble_manager_tick();
}
static int64_t epoch(unsigned day, unsigned hour, unsigned minute, unsigned second)
{ return (int64_t)day*86400 + hour*3600 + minute*60 + second; }
static void sync_at(unsigned day, unsigned h, unsigned m, unsigned s)
{ assert(meal_scheduler_set_time(epoch(day,h,m,s),0)==ESP_OK); }
static void advance(unsigned seconds) { now_us+=(int64_t)seconds*1000000; meal_scheduler_update(); }
static void reset(void)
{
    disk_size=0; now_us=0; writes=0; busy=false; fail_write=false;
    assert(meal_scheduler_init()==ESP_OK);
}
int main(void)
{
    meal_t list[6], due;
    reset();
    weight=(weight_snapshot_t){.sensor_ready=true,.calibrated=true,.tared=true};
    assert(safety_manager_init()==ESP_OK && ble_manager_start()==ESP_OK);
    assert(!meal_scheduler_clock_valid());
    command("SET_MEAL;id=1;time=08:00;mass=60;enable=1");
    assert(count==1 && !strcmp(lines[0],"ACK;cmd=SET_MEAL"));
    command("SET_MEAL;id=2;time=19:00;mass=55;enable=0");
    command("GET_MEALS");
    assert(count==5 && !strcmp(lines[1],"MEALS_BEGIN"));
    assert(!strcmp(lines[2],"MEAL;id=1;time=08:00;mass=60;enable=1"));
    assert(!strcmp(lines[3],"MEAL;id=2;time=19:00;mass=55;enable=0"));
    assert(!strcmp(lines[4],"MEALS_END"));
    command("SET_MEAL;id=1;time=08:01;mass=61.5;enable=0");
    assert(meal_scheduler_get_meals(list,6)==2 && list[0].grammes==61.5f && !list[0].active);
    command("DELETE_MEAL;id=2"); assert(!strcmp(lines[0],"ACK;cmd=DELETE_MEAL"));
    assert(meal_scheduler_init()==ESP_OK && meal_scheduler_get_meals(list,6)==1);
    command("GET_STATUS"); assert(strstr(lines[1],"clock=0"));
    command("SET_MEAL;id=1;time=08:00;mass=60;enable=1");
    advance(86400); assert(!meal_scheduler_take_due(&due));
    sync_at(20000,7,59,58);
    session_active=false; advance(4); /* Planning sans telephone. */
    assert(meal_scheduler_take_due(&due) && due.identifiant==1);
    assert(meal_scheduler_claim(1)==ESP_OK);
    meal_scheduler_report(true);
    unsigned saved=writes; advance(1); assert(writes==saved && !meal_scheduler_take_due(&due));
    assert(meal_scheduler_init()==ESP_OK && !meal_scheduler_clock_valid());
    sync_at(20000,7,59,59); advance(2); assert(!meal_scheduler_take_due(&due));
    assert(meal_scheduler_delete(1)==ESP_OK);
    meal_t meal={.identifiant=1,.heure=8,.grammes=60,.active=true};
    assert(meal_scheduler_set(&meal)==ESP_OK);
    sync_at(20000,7,59,59); advance(2); assert(!meal_scheduler_take_due(&due));
    sync_at(20001,7,59,59); advance(2); assert(meal_scheduler_take_due(&due));
    assert(meal_scheduler_claim(1)==ESP_OK); meal_scheduler_report(false);
    sync_at(19999,7,59,59); advance(2); assert(!meal_scheduler_take_due(&due));
    reset(); assert(meal_scheduler_set(&meal)==ESP_OK);
    sync_at(20000,9,0,0); advance(1); assert(!meal_scheduler_take_due(&due));
    sync_at(20000,7,59,59); advance(62); assert(!meal_scheduler_take_due(&due));
    sync_at(20000,7,59,59); advance(3); assert(meal_scheduler_take_due(&due));
    fail_write=true; assert(meal_scheduler_claim(1)!=ESP_OK);
    meal.grammes=100; assert(meal_scheduler_set(&meal)!=ESP_OK);
    assert(meal_scheduler_get_meals(list,6)==1 && list[0].grammes==60);
    assert(meal_scheduler_delete(1)!=ESP_OK);
    fail_write=false; assert(meal_scheduler_init()==ESP_OK);
    assert(meal_scheduler_get_meals(list,6)==1 && list[0].grammes==60);
    reset(); meal.heure=0; assert(meal_scheduler_set(&meal)==ESP_OK);
    assert(meal_scheduler_set_time(epoch(20000,21,59,59),120)==ESP_OK);
    advance(2); assert(meal_scheduler_take_due(&due));
    assert(meal_scheduler_claim(1)==ESP_OK); meal_scheduler_report(true);
    advance(7*86400); assert(!meal_scheduler_clock_valid() && !meal_scheduler_take_due(&due));
    meal.grammes=NAN; assert(meal_scheduler_set(&meal)==ESP_ERR_INVALID_ARG);
    session_active=true;
    command("SET_TIME;epoch=1792150000;offset=120"); assert(!strcmp(lines[0],"ACK;cmd=SET_TIME"));
    command("GET_STATUS"); assert(strstr(lines[1],"clock=1"));
    assert(strstr(lines[1],"last=42.0")); /* L'application attend des grammes. */
    command("MANUAL_FEED;mass=20"); assert(busy && !strcmp(lines[0],"ACK;cmd=MANUAL_FEED"));
    saved=requests; command("MANUAL_FEED;mass=30"); assert(requests==saved && !strcmp(lines[0],"ERR;code=BUSY"));
    assert(!safety_manager_can_feed(60));
    assert(command("STOP") && !busy);
    weight.calibrated=false;
    command("MANUAL_FEED;mass=20"); assert(!strcmp(lines[0],"ERR;code=SAFETY"));
    safety_manager_latch_fault(SAFETY_FAULT_TIMEOUT);
    command("RESET_FAULT"); assert(!strcmp(lines[0],"ACK;cmd=RESET_FAULT"));
    assert(safety_manager_get_fault()==SAFETY_FAULT_NONE);
    reset();
    meal=(meal_t){.identifiant=1,.heure=8,.grammes=1,.active=false};
    for (uint8_t id=1; id<=6; ++id) {
        meal.identifiant=id; assert(meal_scheduler_set(&meal)==ESP_OK);
    }
    command("GET_MEALS"); assert(count==9);
    sync_at(20000,7,59,59); advance(2); assert(!meal_scheduler_take_due(&due));
    meal.identifiant=7; assert(meal_scheduler_set(&meal)==ESP_ERR_INVALID_ARG);
    meal.identifiant=1; meal.grammes=201; assert(meal_scheduler_set(&meal)==ESP_ERR_INVALID_ARG);
    /* Un blob inconnu ne doit jamais etre interprete comme un planning valide. */
    disk[0]=99; assert(meal_scheduler_init()==ESP_ERR_INVALID_STATE);
    assert(meal_scheduler_get_meals(list,6)==0 && !meal_scheduler_take_due(&due));
    ble_manager_set_features(false,false);
    command("TARE"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("CALIBRATE;mass_ref=100"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("MANUAL_FEED;mass=20"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("SET_MEAL;id=1;time=08:00;mass=60;enable=1"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("GET_MEALS"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("DELETE_MEAL;id=1"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("SET_TIME;epoch=1792150000;offset=60"); assert(!strcmp(lines[0],"ERR;code=DISABLED"));
    command("GET_STATUS"); assert(count==2 && !strcmp(lines[0],"ACK;cmd=GET_STATUS"));
    assert(command("STOP")); /* STOP reste disponible. */
    ble_manager_set_features(true,true);
    puts("Scheduler + BLE commands + NVS + safety: PASS (no motor)");
    return 0;
}
