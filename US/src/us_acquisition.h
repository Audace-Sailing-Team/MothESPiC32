#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    US_SAMPLE_VALID = 0,
    US_SAMPLE_NO_ECHO,
    US_SAMPLE_RX_TIMEOUT
} us_sample_status_t;

typedef struct {
    uint32_t sequence;

    int64_t trig_us;
    int64_t capture_done_us;

    // Timestamp stimati dal completamento RMT
    int64_t echo_rise_us_est;
    int64_t echo_fall_us_est;

    // Durata ECHO HIGH in microsecondi
    uint32_t echo_high_us;

    uint16_t rmt_symbols;

    us_sample_status_t status;

} us_sample_t;

esp_err_t us_acquisition_start(void);

bool us_acquisition_read(
    us_sample_t *sample,
    TickType_t timeout
);

uint32_t us_acquisition_dropped(void);