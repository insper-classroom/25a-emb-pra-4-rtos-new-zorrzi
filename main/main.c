#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "ssd1306.h"
#include "gfx.h"

#include <stdio.h>

#define TRIGGER_PIN 17
#define ECHO_PIN 16

#define I2C_PORT i2c0
#define OLED_ADDR 0x3C

SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueDistance;
QueueHandle_t xQueueTime;

ssd1306_t oled;

void pin_callback(uint gpio, uint32_t events) {
    absolute_time_t echo_start;
    absolute_time_t echo_end;

    if (events & GPIO_IRQ_EDGE_RISE) {
        echo_start = get_absolute_time();
        xQueueSendFromISR(xQueueTime, &echo_start, 0);
    }
    if (events & GPIO_IRQ_EDGE_FALL) {
        echo_end = get_absolute_time();
        xQueueSendFromISR(xQueueTime, &echo_end, 0);
    }
}

void echo_task(void *p) {
    absolute_time_t start_time, end_time;

    while (1) {
        if (xQueueReceive(xQueueTime, &start_time, portMAX_DELAY) == pdTRUE) {
            if (xQueueReceive(xQueueTime, &end_time, portMAX_DELAY) == pdTRUE) {
                int64_t dt_us = absolute_time_diff_us(start_time, end_time);
                float distance = (dt_us * 0.0343f) / 2.0f;

                xQueueSend(xQueueDistance, &distance, 0);
            }
        }
    }
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    while (true) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(10);
        gpio_put(TRIGGER_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void oled_task(void *p) {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);

    ssd1306_init();
    gfx_init(&oled, 128, 32);

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY) == pdTRUE) {
            float dist;
            if (xQueueReceive(xQueueDistance, &dist, 0)) {
                gfx_clear_buffer(&oled);

                if(dist > 400){
                    
                    gfx_draw_string(&oled, 0, 0, 1, "Falha");
                    gfx_show(&oled);
                }
                else{
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Dist: %.2f cm", dist);
                    gfx_draw_string(&oled, 0, 0, 1, buf);
    
                    int bar = dist < 100 ? dist : 100;
                    gfx_draw_line(&oled, 15, 27, 15 + bar, 27);
    
                    gfx_show(&oled);
                }
            }
        }
    }
}



int main() {
    stdio_init_all();

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xQueueDistance = xQueueCreate(10, sizeof(float));
    xQueueTime = xQueueCreate(10, sizeof(absolute_time_t));

    xTaskCreate(trigger_task, "Trigger Task", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo Task", 512, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED Task", 512, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}
