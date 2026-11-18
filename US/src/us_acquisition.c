#include "us_acquisition.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define TAG "US_ACQ"

// GPIO
#define TRIG_GPIO               GPIO_NUM_5
#define ECHO_GPIO               GPIO_NUM_18

// RMT
#define RMT_RESOLUTION_HZ       1000000
#define RMT_MEM_SYMBOLS         64

// AJ-SR04M
#define TRIG_PULSE_US           12

// Timeout di inattività RMT (10 ms)
#define RMT_IDLE_US             10000
// Timeout massimo di attesa task (40 ms)
#define RX_WAIT_TIMEOUT_MS      40

// Buffer
#define RINGBUF_SIZE            8192
#define ACQ_TASK_STACK          4096
#define ACQ_TASK_PRIORITY       10

// --------------------------------------------------
// GLOBALS
// --------------------------------------------------
static rmt_channel_handle_t s_rx_channel = NULL;
static RingbufHandle_t s_ringbuf = NULL;
static TaskHandle_t s_acq_task = NULL;
static rmt_symbol_word_t s_rx_symbols[RMT_MEM_SYMBOLS];
static volatile size_t s_rx_num_symbols = 0;
static volatile int64_t s_rx_done_us = 0;
static uint32_t s_sequence = 0;
static volatile uint32_t s_dropped = 0;

// --------------------------------------------------
// RMT RX CALLBACK
// --------------------------------------------------
static bool rmt_rx_done_callback(
    rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t *edata,
    void *user_ctx)
{
    BaseType_t high_task_woken = pdFALSE;
    
    s_rx_num_symbols = edata->num_symbols;
    s_rx_done_us = esp_timer_get_time();
    
    vTaskNotifyGiveFromISR(
        s_acq_task,
        &high_task_woken
    );
    
    return high_task_woken == pdTRUE;
}

// --------------------------------------------------
// RMT & GPIO INITIALIZATION
// --------------------------------------------------
static esp_err_t init_rmt(void)
{
    // GPIO TRIG CONFIGURATION
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TRIG_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(TRIG_GPIO, 0));

    // RX CONFIGURATION
    rmt_rx_channel_config_t rx_config = {
        .gpio_num = ECHO_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = RMT_MEM_SYMBOLS,
        .flags.invert_in = false,
        .flags.with_dma = false
    };
    ESP_ERROR_CHECK(
        rmt_new_rx_channel(
            &rx_config,
            &s_rx_channel
        )
    );

    // RX CALLBACK
    rmt_rx_event_callbacks_t rx_callbacks = {
        .on_recv_done = rmt_rx_done_callback
    };
    ESP_ERROR_CHECK(
        rmt_rx_register_event_callbacks(
            s_rx_channel,
            &rx_callbacks,
            NULL
        )
    );

    ESP_ERROR_CHECK(rmt_enable(s_rx_channel));

    ESP_LOGI(TAG, "RMT initialized");
    ESP_LOGI(TAG, "Resolution: 1 MHz");
    ESP_LOGI(TAG, "TRIG: GPIO%d (Direct)", TRIG_GPIO);
    ESP_LOGI(TAG, "ECHO: GPIO%d (RMT RX)", ECHO_GPIO);
    
    return ESP_OK;
}

// --------------------------------------------------
// ECHO HIGH EXTRACTION
// --------------------------------------------------
static uint32_t extract_echo_high(
    const rmt_symbol_word_t *symbols,
    size_t count)
{
    uint32_t current_high = 0;
    uint32_t maximum_high = 0;
    
    for (size_t i = 0; i < count; i++) {
        uint16_t duration[2] = {
            symbols[i].duration0,
            symbols[i].duration1
        };
        uint8_t level[2] = {
            symbols[i].level0,
            symbols[i].level1
        };
        
        for (int j = 0; j < 2; j++) {
            if (level[j] == 1) {
                current_high += duration[j];
            } else {
                if (current_high > maximum_high)
                    maximum_high = current_high;
                current_high = 0;
            }
        }
    }
    
    if (current_high > maximum_high)
        maximum_high = current_high;
        
    return maximum_high;
}

// --------------------------------------------------
// RING BUFFER PUSH
// --------------------------------------------------
static void push_sample(const us_sample_t *sample)
{
    if (xRingbufferSend(
            s_ringbuf,
            sample,
            sizeof(us_sample_t),
            0) != pdTRUE) {
        s_dropped++;
    }
}

// --------------------------------------------------
// ACQUISITION TASK
// --------------------------------------------------
static void acquisition_task(void *arg)
{
    s_acq_task = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(init_rmt());

    // Stabilizzazione sensore
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Acquisition started");

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = RMT_IDLE_US * 1000
    };

    for (;;) {
        us_sample_t sample = {0};
        sample.sequence = s_sequence++;

        // Pulisce eventuali notifiche precedenti
        ulTaskNotifyTake(pdTRUE, 0);
        s_rx_num_symbols = 0;
        s_rx_done_us = 0;

        // ------------------------------------------
        // 1. ARM RMT RX
        // ------------------------------------------
        esp_err_t err = rmt_receive(
            s_rx_channel,
            s_rx_symbols,
            sizeof(s_rx_symbols),
            &receive_config
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RX error: %s",
                     esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // ------------------------------------------
        // 2. GENERATE TRIG (GPIO Direct)
        // ------------------------------------------
        sample.trig_us = esp_timer_get_time();
        gpio_set_level(TRIG_GPIO, 1);
        esp_rom_delay_us(TRIG_PULSE_US);
        gpio_set_level(TRIG_GPIO, 0);

        // ------------------------------------------
        // 3. WAIT FOR RMT RX COMPLETION
        // ------------------------------------------
        uint32_t notification = ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(RX_WAIT_TIMEOUT_MS)
        );
        if (notification == 0) {
            sample.status = US_SAMPLE_RX_TIMEOUT;
            rmt_disable(s_rx_channel);
            rmt_enable(s_rx_channel);
            push_sample(&sample);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // ------------------------------------------
        // 4. READ CAPTURE
        // ------------------------------------------
        size_t count = s_rx_num_symbols;
        int64_t capture_time = s_rx_done_us;
        sample.capture_done_us = capture_time;
        sample.rmt_symbols = (uint16_t)count;
        
        uint32_t echo_high = extract_echo_high(
            s_rx_symbols,
            count
        );

        // ------------------------------------------
        // 5. VALIDATION
        // ------------------------------------------
        if (echo_high == 0) {
            sample.status = US_SAMPLE_NO_ECHO;
            push_sample(&sample);
            // Riarmo conservativo dopo assenza di eco
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // ------------------------------------------
        // 6. BUILD MEASUREMENT
        // ------------------------------------------
        sample.status = US_SAMPLE_VALID;
        sample.echo_high_us = echo_high;

        // Stima temporale:
        // RMT termina dopo RMT_IDLE_US di inattività.
        sample.echo_fall_us_est =
            capture_time - RMT_IDLE_US;
        sample.echo_rise_us_est =
            sample.echo_fall_us_est - echo_high;

        // ------------------------------------------
        // 7. PUSH TO RING BUFFER
        // ------------------------------------------
        push_sample(&sample);
        
        // Nessun delay aggiuntivo:
        // la cattura è terminata dopo 10 ms
        // dall'ultimo fronte ECHO.
    }
}

// --------------------------------------------------
// PUBLIC API
// --------------------------------------------------
esp_err_t us_acquisition_start(void)
{
    if (s_ringbuf != NULL)
        return ESP_ERR_INVALID_STATE;

    s_ringbuf = xRingbufferCreate(
        RINGBUF_SIZE,
        RINGBUF_TYPE_NOSPLIT
    );
    if (s_ringbuf == NULL)
        return ESP_ERR_NO_MEM;

    BaseType_t result = xTaskCreate(
        acquisition_task,
        "us_acquisition",
        ACQ_TASK_STACK,
        NULL,
        ACQ_TASK_PRIORITY,
        NULL
    );

    if (result != pdPASS) {
        vRingbufferDelete(s_ringbuf);
        s_ringbuf = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// --------------------------------------------------
bool us_acquisition_read(
    us_sample_t *sample,
    TickType_t timeout)
{
    if (sample == NULL || s_ringbuf == NULL)
        return false;

    size_t item_size = 0;
    void *item = xRingbufferReceive(
        s_ringbuf,
        &item_size,
        timeout
    );

    if (item == NULL)
        return false;

    bool valid = (item_size == sizeof(us_sample_t));
    if (valid)
        memcpy(sample, item, sizeof(us_sample_t));

    vRingbufferReturnItem(s_ringbuf, item);
    return valid;
}

// --------------------------------------------------
uint32_t us_acquisition_dropped(void)
{
    return s_dropped;
}