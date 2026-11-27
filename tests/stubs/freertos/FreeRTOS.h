/* Types FreeRTOS et sections critiques adaptés au banc de test natif. */
#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#define pdTRUE 1
#define pdPASS 1
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#endif
