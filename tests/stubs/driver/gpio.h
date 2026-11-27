/* Types et fonctions GPIO remplacés par les tests exécutés sur PC. */
#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdint.h>
#include "esp_err.h"
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 0
typedef struct { uint64_t pin_bit_mask; int mode; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(int pin, unsigned level);
int gpio_get_level(int pin);
#endif
