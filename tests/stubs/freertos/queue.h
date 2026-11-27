#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include <stddef.h>
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, size_t);
void vQueueDelete(QueueHandle_t);
int xQueueOverwrite(QueueHandle_t, const void *);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
#endif
