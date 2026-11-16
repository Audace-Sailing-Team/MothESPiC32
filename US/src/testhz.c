
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define TRIG_GPIO GPIO_NUM_5
#define ECHO_GPIO GPIO_NUM_18

#define TRIG_PULSE_US       12
#define TEST_DURATION_US   5000000LL

#define ECHO_RISE_TIMEOUT_US 30000
#define ECHO_FALL_TIMEOUT_US 30000

#define NUM_INTERVALS 6

static const char *TAG = "ULTRASONIC";

// Intervalli di attesa dopo ECHO completo
static const uint32_t intervals_ms[NUM_INTERVALS] = {
    0, 2, 5, 10, 15, 20
};

typedef struct {
    uint32_t trig;
    uint32_t completed;
    uint32_t timeout_rise;
    uint32_t timeout_fall;

    uint64_t response_total;
    uint64_t response_min;
    uint64_t response_max;

    uint64_t cycle_total;
    uint64_t cycle_min;
    uint64_t cycle_max;
} test_stats_t;

static bool wait_gpio_level(gpio_num_t pin,
                            int level,
                            int64_t timeout_us,
                            int64_t *timestamp)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(pin) != level) {
        if ((esp_timer_get_time() - start) >= timeout_us) {
            return false;
        }
        esp_rom_delay_us(2);
    }

    *timestamp = esp_timer_get_time();
    return true;
}

static void init_gpio(void)
{
    gpio_config_t trig_config = {
        .pin_bit_mask = (1ULL << TRIG_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&trig_config));

    gpio_config_t echo_config = {
        .pin_bit_mask = (1ULL << ECHO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&echo_config));

    gpio_set_level(TRIG_GPIO, 0);
}

static void reset_stats(test_stats_t *s)
{
    s->trig = 0;
    s->completed = 0;
    s->timeout_rise = 0;
    s->timeout_fall = 0;

    s->response_total = 0;
    s->response_min = UINT64_MAX;
    s->response_max = 0;

    s->cycle_total = 0;
    s->cycle_min = UINT64_MAX;
    s->cycle_max = 0;
}

static void run_interval_test(uint32_t wait_ms)
{
    test_stats_t s;
    reset_stats(&s);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "TEST ATTESA DOPO ECHO: %" PRIu32 " ms", wait_ms);
    ESP_LOGI(TAG, "Durata: 5 secondi");
    ESP_LOGI(TAG, "==============================================");

    int64_t test_start = esp_timer_get_time();
    int64_t test_end = test_start + TEST_DURATION_US;

    int64_t previous_echo_fall = 0;

    while (esp_timer_get_time() < test_end) {

        // Assicura che ECHO sia LOW prima del nuovo TRIG.
        if (gpio_get_level(ECHO_GPIO) != 0) {
            int64_t t;
            if (!wait_gpio_level(ECHO_GPIO, 0,
                                 ECHO_FALL_TIMEOUT_US, &t)) {
                s.timeout_fall++;
                continue;
            }
        }

        int64_t trig_time = esp_timer_get_time();

        gpio_set_level(TRIG_GPIO, 1);
        esp_rom_delay_us(TRIG_PULSE_US);
        gpio_set_level(TRIG_GPIO, 0);

        s.trig++;

        int64_t rise_time = 0;

        if (!wait_gpio_level(ECHO_GPIO, 1,
                             ECHO_RISE_TIMEOUT_US, &rise_time)) {
            s.timeout_rise++;

            // Nessun ritardo aggiuntivo dopo un tentativo fallito:
            // il timeout è già trascorso.
            continue;
        }

        int64_t fall_time = 0;

        if (!wait_gpio_level(ECHO_GPIO, 0,
                             ECHO_FALL_TIMEOUT_US, &fall_time)) {
            s.timeout_fall++;
            continue;
        }

        s.completed++;

        uint64_t response =
            (uint64_t)(fall_time - trig_time);

        s.response_total += response;

        if (response < s.response_min)
            s.response_min = response;

        if (response > s.response_max)
            s.response_max = response;

        if (previous_echo_fall != 0) {
            uint64_t cycle =
                (uint64_t)(fall_time - previous_echo_fall);

            s.cycle_total += cycle;

            if (cycle < s.cycle_min)
                s.cycle_min = cycle;

            if (cycle > s.cycle_max)
                s.cycle_max = cycle;
        }

        previous_echo_fall = fall_time;

        // Attesa richiesta dopo la fine dell'ECHO.
        if (wait_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
    }

    int64_t duration = esp_timer_get_time() - test_start;

    double frequency =
        s.completed * 1000000.0 / (double)duration;

    double success_rate =
        s.trig > 0
        ? 100.0 * s.completed / s.trig
        : 0.0;

    ESP_LOGI(TAG, "RISULTATI ATTESA %" PRIu32 " ms", wait_ms);
    ESP_LOGI(TAG, "Durata: %.3f s", duration / 1000000.0);

    ESP_LOGI(TAG, "TRIG: %" PRIu32, s.trig);
    ESP_LOGI(TAG, "ECHO completi: %" PRIu32, s.completed);

    ESP_LOGI(TAG, "Timeout salita: %" PRIu32,
             s.timeout_rise);

    ESP_LOGI(TAG, "Timeout discesa: %" PRIu32,
             s.timeout_fall);

    ESP_LOGI(TAG, "Percentuale risposta: %.2f%%",
             success_rate);

    ESP_LOGI(TAG, "Frequenza ECHO: %.2f misure/s",
             frequency);

    if (s.completed > 0) {
        ESP_LOGI(TAG, "Risposta TRIG -> fine ECHO: media=%" PRIu64
                 " min=%" PRIu64 " max=%" PRIu64 " us",
                 s.response_total / s.completed,
                 s.response_min,
                 s.response_max);
    }

    if (s.completed > 1) {
        uint32_t n = s.completed - 1;

        ESP_LOGI(TAG, "Ciclo ECHO-ECHO: media=%" PRIu64
                 " min=%" PRIu64 " max=%" PRIu64 " us",
                 s.cycle_total / n,
                 s.cycle_min,
                 s.cycle_max);
    }

    ESP_LOGI(TAG, "==============================================");

    // Pausa tra le prove per stabilizzare il sensore.
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void app_main(void)
{
    init_gpio();

    ESP_LOGI(TAG, "Attendo stabilizzazione sensore...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (int i = 0; i < NUM_INTERVALS; i++) {
        run_interval_test(intervals_ms[i]);
    }

    ESP_LOGI(TAG, "TUTTI I TEST COMPLETATI");
}