#include "us_acquisition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "DEBUG"
#define DEBUG_TASK_STACK    4096
#define DEBUG_TASK_PRIORITY 5

static void debug_task(void *arg)
{
    us_sample_t sample;
    uint32_t count = 0;
    
    ESP_LOGI(TAG, "Debug task started");
    
    for (;;) {
        if (us_acquisition_read(&sample, pdMS_TO_TICKS(100))) {
            count++;
            
            // Print dettagliato ogni campione
            ESP_LOGI(TAG, "--- Sample #%u ---", sample.sequence);
            ESP_LOGI(TAG, "Status: %d", sample.status);
            ESP_LOGI(TAG, "Echo HIGH: %lu us", sample.echo_high_us);
            ESP_LOGI(TAG, "RMT symbols: %u", sample.rmt_symbols);
            ESP_LOGI(TAG, "TRIG timestamp: %lld us", sample.trig_us);
            ESP_LOGI(TAG, "Capture done: %lld us", sample.capture_done_us);
            ESP_LOGI(TAG, "Echo rise (est): %lld us", sample.echo_rise_us_est);
            ESP_LOGI(TAG, "Echo fall (est): %lld us", sample.echo_fall_us_est);
            
            // Calcola distanza (velocità suono = 343 m/s = 0.343 mm/us)
            if (sample.status == US_SAMPLE_VALID) {
                float distance_mm = (sample.echo_high_us * 0.343f) / 2.0f;
                ESP_LOGI(TAG, "Distance: %.2f mm", distance_mm);
            }
            
            // Print riassuntivo ogni 10 campioni
            if (count % 10 == 0) {
                ESP_LOGI(TAG, "=== Total samples received: %u ===", count);
                ESP_LOGI(TAG, "=== Dropped samples: %u ===", us_acquisition_dropped());
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(us_acquisition_start());
    
    // Avvia il task di debug
    xTaskCreate(
        debug_task,
        "debug_task",
        DEBUG_TASK_STACK,
        NULL,
        DEBUG_TASK_PRIORITY,
        NULL
    );
}