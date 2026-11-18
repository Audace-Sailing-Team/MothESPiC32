#include <stdbool.h>
#include <stdio.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Numeri GPIO, non posizioni fisiche dei pin sulla scheda. */
#define GPS_UART UART_NUM_2
#define GPS_RX_PIN 16 /* RX2 sulla dev board: collegare al TX del GPS. */
#define GPS_TX_PIN 17 /* TX2 sulla dev board: verso RX del GPS, facoltativo. */
#define GPS_BAUD_RATE 9600
#define GPS_RX_BUFFER_SIZE 2048

static const char *const TAG = "gps";

void app_main(void)
{
    const uart_config_t config = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* UART2 riceve il GPS; UART0 rimane alla console USB della dev board. */
    ESP_ERROR_CHECK(uart_param_config(GPS_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART, GPS_TX_PIN, GPS_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART, GPS_RX_BUFFER_SIZE, 0,
                                        0, NULL, 0));

    ESP_LOGI(TAG, "Lettore GPS avviato: UART2, RX=GPIO%d, TX=GPIO%d, %d baud",
             GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_RATE);
    ESP_LOGI(TAG, "Console a 115200 baud. In attesa dei messaggi NMEA...");

    unsigned char buffer[256];
    TickType_t last_activity = xTaskGetTickCount();
    bool receiving = false;

    for (;;) {
        const int count = uart_read_bytes(GPS_UART, buffer, sizeof(buffer),
                                          pdMS_TO_TICKS(250));
        if (count < 0) {
            ESP_LOGE(TAG, "Errore di lettura UART2");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (count > 0) {
            if (!receiving) {
                ESP_LOGI(TAG, "Ricezione attiva; seguono i byte del GPS:");
                receiving = true;
            }
            /* count e' la lunghezza: il blocco non e' una stringa terminata da 0. */
            if (fwrite(buffer, 1, (size_t)count, stdout) != (size_t)count ||
                fflush(stdout) != 0) {
                ESP_LOGE(TAG, "Errore di scrittura sulla console");
                clearerr(stdout);
            }
            last_activity = xTaskGetTickCount();
        } else if ((TickType_t)(xTaskGetTickCount() - last_activity) >=
                   pdMS_TO_TICKS(5000)) {
            ESP_LOGW(TAG, "Nessun byte GPS da 5 secondi: verificare TX/RX, GND e baud");
            receiving = false;
            last_activity = xTaskGetTickCount();
        }
    }
}
