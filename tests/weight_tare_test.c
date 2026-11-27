/* Simule le HX711 et la NVS pour exercer la tare et la calibration. */
/* Vrai HX711 / WeightManager ; broches et NVS simulees. */
#include "weight_manager.h"
#include "driver/gpio.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int64_t now;
static int32_t raw = 1200;
static unsigned bit, saves;
static bool clock_high, ready = true, moteur;
int64_t esp_timer_get_time(void) { return now; }
void esp_rom_delay_us(unsigned us) { now += us; }
bool motor_controller_is_running(void) { return moteur; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
esp_err_t gpio_set_level(int pin, unsigned level)
{
    assert(pin==25);
    if (!level && clock_high) bit=(bit+1)%25;
    clock_high=level!=0; return ESP_OK;
}
int gpio_get_level(int pin)
{
    assert(pin==34);
    if (!clock_high) return ready ? 0 : 1;
    assert(bit<24); return (int)(((uint32_t)raw >> (23-bit)) & 1);
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{ (void)mode; assert(!strcmp(name,"croq_poids")); *h=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h; (void)key;
    struct { int32_t zero; float coefficient; } calibration = {1000, 4};
    assert(*size==sizeof(calibration)); memcpy(out,&calibration,sizeof(calibration)); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *in, size_t size)
{ (void)h; (void)key; (void)in; assert(size==8); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; ++saves; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
static void readings(unsigned n) { while(n--) weight_manager_update(); }
int main(void)
{
    weight_manager_init(); weight_manager_update();
    assert(weight_manager_get_snapshot().grams==50);
    assert(weight_manager_demander_tare());
    readings(19); assert(weight_manager_is_busy() && weight_manager_tare_count()==0);
    readings(1); assert(!weight_manager_is_busy() && weight_manager_tare_count()==1);
    assert(weight_manager_get_snapshot().grams==0 && saves==0);
    raw=1240; readings(5); assert(weight_manager_get_snapshot().grams==10);
    /* Une annulation ne doit pas pouvoir etre prise pour une nouvelle tare. */
    assert(weight_manager_demander_tare()); readings(5); weight_manager_cancel_operation();
    assert(!weight_manager_is_busy() && weight_manager_tare_count()==1);
    assert(weight_manager_demander_tare()); ready=false;
    now+=10000000; weight_manager_update();
    assert(!weight_manager_is_busy() && weight_manager_tare_count()==1 && saves==0);
    moteur=true; assert(!weight_manager_demander_tare()); moteur=false;
    ready=true; raw=2000;
    assert(weight_manager_demander_tare()); readings(20);
    assert(weight_manager_tare_count()==2 && weight_manager_get_snapshot().grams==0 && saves==0);
    /* Calibration sans masse : rejet, calibration precedente conservee. */
    assert(weight_manager_demander_calibration(100)); readings(20);
    assert(weight_manager_get_snapshot().operation_result==3);
    raw=2400;
    assert(weight_manager_demander_calibration(100)); readings(20);
    assert(weight_manager_get_snapshot().operation_result==0 && saves==1);
    assert(weight_manager_get_snapshot().grams==100);
    now+=1000000; ready=false; weight_manager_update();
    assert(!weight_manager_get_snapshot().sensor_ready);
    assert(!weight_manager_demander_calibration(0));
    puts("WeightManager: 20 samples/success counter/cancel/10s timeout/new tare PASS");
}
