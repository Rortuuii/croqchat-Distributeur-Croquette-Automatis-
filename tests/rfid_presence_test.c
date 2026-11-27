/* Simule les réponses UART pour vérifier l’UID, le retrait et l’expiration de présence. */
/* Vrai pilote RFID ; transport UART simule, aucune action physique. */
#include "cat_identification.h"
#include "driver/uart.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static int64_t now;
static bool carte = true, panne;
static void (*rfid_task)(void *);
static jmp_buf fin_scan;
static uint8_t entree[32];
static unsigned taille_entree, position;
int64_t esp_timer_get_time(void) { return now; }
void esp_rom_delay_us(unsigned us) { now += us; }
void vTaskDelay(TickType_t ms)
{
    if (ms == 200) longjmp(fin_scan, 1); /* Un scan publie, sortir de la boucle infinie. */
    now += (int64_t)ms * 1000;
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    rfid_task = task; return pdPASS;
}
esp_err_t uart_param_config(int port, const uart_config_t *c)
{ assert(port == 2 && c->baud_rate == 57600); return ESP_OK; }
esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts)
{ (void)port; (void)rts; (void)cts; assert(tx == 17 && rx == 16); return ESP_OK; }
esp_err_t uart_driver_install(int p, int rx, int tx, int n, void *q, int f)
{ (void)p; (void)rx; (void)tx; (void)n; (void)q; (void)f; return ESP_OK; }
esp_err_t uart_flush_input(int p) { (void)p; taille_entree = position = 0; return ESP_OK; }
esp_err_t uart_wait_tx_done(int p, TickType_t t) { (void)p; (void)t; return ESP_OK; }
esp_err_t uart_set_line_inverse(int p, unsigned m) { (void)p; (void)m; return ESP_OK; }
int uart_write_bytes(int port, const void *bytes, int size)
{
    (void)port;
    const uint8_t *c = bytes;
    memset(entree, 0, sizeof(entree)); position = 0;
    if (panne) { taille_entree = 0; return size; }
    if (size == 1) { entree[0] = 0x55; taille_entree = 1; }
    else if (c[0] == 2 || c[0] == 9) { taille_entree = 2; }
    else if (size == 4 && c[2] == 0x26) {
        entree[0] = carte ? 0x80 : 0x87;
        entree[1] = carte ? 5 : 0;
        entree[2] = 4; entree[4] = 0x28;
        taille_entree = carte ? 7 : 2;
    } else if (size == 5 && c[3] == 0x20) {
        uint8_t reponse[] = {0x80, 8, 4, 0xAB, 0xCD, 0xEF, 4 ^ 0xAB ^ 0xCD ^ 0xEF, 0x28, 0, 0};
        memcpy(entree, reponse, sizeof(reponse)); taille_entree = sizeof(reponse);
    } else if (size == 10 && c[3] == 0x70) {
        entree[0] = 0x80; entree[1] = 6; entree[5] = 8; taille_entree = 8;
    } else { assert(!"Unexpected RFID command"); }
    return size;
}
int uart_read_bytes(int p, void *out, unsigned n, TickType_t timeout)
{
    (void)p;
    unsigned restant = taille_entree - position;
    if (n > restant) n = restant;
    memcpy(out, entree + position, n); position += n;
    if (!n) now += (int64_t)timeout * 1000;
    return (int)n;
}
static void scan(void)
{
    if (setjmp(fin_scan) == 0) rfid_task(NULL);
}
int main(void)
{
    assert(!cat_identification_get_snapshot().badge_present);
    assert(cat_identification_init() == ESP_OK && rfid_task);
    scan();
    cat_identification_snapshot_t badge = cat_identification_get_snapshot();
    assert(badge.badge_present && !strcmp(badge.identifiant_badge, "04ABCDEF"));
    now += 1999999; assert(cat_identification_get_snapshot().badge_present);
    now += 1; assert(!cat_identification_get_snapshot().badge_present);
    assert(!cat_identification_get_snapshot().identifiant_badge[0]);
    scan(); assert(cat_identification_get_snapshot().badge_present);
    carte = false; scan(); assert(cat_identification_get_snapshot().badge_present);
    now += 2000000; scan(); assert(!cat_identification_get_snapshot().badge_present);
    carte = true; scan(); assert(cat_identification_get_snapshot().badge_present);
    panne = true; scan(); assert(cat_identification_get_snapshot().badge_present);
    now += 2000000; scan(); assert(!cat_identification_get_snapshot().badge_present);
    puts("RFID driver: UID/presence/removal/2s expiry/reader failure PASS");
}
