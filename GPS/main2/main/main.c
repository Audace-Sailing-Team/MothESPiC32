#include <stdbool.h>
#include <stdio.h>

#include "gps_nmea.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Imposta UART2 per il GPS, con ricezione su GPIO16 e trasmissione su GPIO17.
#define GPS_UART UART_NUM_2
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD_RATE 9600
#define GPS_RX_BUFFER_SIZE 2048

static const char *const TAG = "gps";

// Mostra sulla console i dati contenuti nella frase GPS appena ricevuta.
static void print_reading(gps_nmea_result_t type, const gps_reading_t *reading)
{
    // Prepara data e ora; i campi mancanti restano indicati con i trattini.
    char time[40] = "--:--:--";
    char date[40] = "--/--/----";
    if (reading->has_time) {
        snprintf(time, sizeof(time), "%02d:%02d:%02d",
                 reading->hour, reading->minute, reading->second);
    }
    if (reading->has_date) {
        snprintf(date, sizeof(date), "%02d/%02d/%04d",
                 reading->day, reading->month, reading->year);
    }
    // Se manca una posizione valida, mostra NESSUN FIX e termina la stampa.
    if (!reading->valid) {
        ESP_LOGW(TAG, "%s | NESSUN FIX | %s %s UTC",
                 type == GPS_NMEA_RMC ? "RMC" : "GGA", date, time);
        return;
    }

    // Per una frase RMC mostra posizione, velocita', rotta, data e ora.
    if (type == GPS_NMEA_RMC) {
        char speed[32] = "--";
        char course[32] = "--";
        if (reading->has_speed)
            snprintf(speed, sizeof(speed), "%.2f", reading->speed_kmh);
        if (reading->has_course)
            snprintf(course, sizeof(course), "%.2f", reading->course_deg);
        ESP_LOGI(TAG, "RMC | FIX OK | Lat: %.6f | Lon: %.6f | Vel: %s km/h | "
                 "Rotta: %s deg | %s %s UTC",
                 reading->latitude_deg, reading->longitude_deg, speed, course, date, time);
    } else {
        // Per una frase GGA mostra posizione, quota, satelliti e qualita' del fix.
        char altitude[32] = "--";
        char hdop[32] = "--";
        char satellites[16] = "--";
        if (reading->has_altitude)
            snprintf(altitude, sizeof(altitude), "%.1f", reading->altitude_m);
        if (reading->has_hdop)
            snprintf(hdop, sizeof(hdop), "%.2f", reading->hdop);
        if (reading->has_satellites)
            snprintf(satellites, sizeof(satellites), "%d", reading->satellites);
        ESP_LOGI(TAG, "GGA | FIX OK | Lat: %.6f | Lon: %.6f | Alt: %s m | "
                 "Sat usati: %s | HDOP: %s | Qualita: %d | %s UTC",
                 reading->latitude_deg, reading->longitude_deg, altitude,
                 satellites, hdop, reading->fix_quality, time);
    }
}

void app_main(void)
{
    // Configura la seriale GPS a 9600 baud, con 8 bit dati, nessuna parita' e 1 bit di stop.
    const uart_config_t config = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Applica la configurazione, collega la UART ai pin e attiva il buffer di ricezione.
    ESP_ERROR_CHECK(uart_param_config(GPS_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART, GPS_TX_PIN, GPS_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART, GPS_RX_BUFFER_SIZE, 0,
                                        0, NULL, 0));

    ESP_LOGI(TAG, "Lettore GPS avviato: UART2, RX=GPIO%d, TX=GPIO%d, %d baud",
             GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_RATE);
    ESP_LOGI(TAG, "Console a 115200 baud. In attesa dei messaggi NMEA...");

    // Prepara lo spazio per i byte in arrivo e lo stato che ricostruisce le frasi GPS.
    unsigned char buffer[256];
    gps_nmea_parser_t parser = {0};
    gps_reading_t reading;

    // Memorizza quando arrivano dati per poter riconoscere un'interruzione della ricezione.
    TickType_t last_activity = xTaskGetTickCount();
    bool receiving = false;

    for (;;) {
        // Legge fino a 256 byte dal GPS, aspettando al massimo 250 millisecondi.
        const int count = uart_read_bytes(GPS_UART, buffer, sizeof(buffer),
                                          pdMS_TO_TICKS(250));
        // In caso di errore scarta la frase parziale e riprova dopo un secondo.
        if (count < 0) {
            ESP_LOGE(TAG, "Errore di lettura UART2");
            gps_nmea_reset(&parser);

            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (count > 0) {
            // Segnala l'inizio della ricezione, senza ripetere l'avviso per ogni blocco.
            if (!receiving) {
                ESP_LOGI(TAG, "Ricezione attiva; conversione dei messaggi RMC e GGA");
                receiving = true;
            }

            // Passa i byte al parser, che li raccoglie fino a ottenere una frase completa.
            for (int i = 0; i < count; ++i) {
                const gps_nmea_result_t result = gps_nmea_feed(&parser, buffer[i], &reading);

                // Stampa le frasi accettate oppure segnala dati errati o righe troppo lunghe.
                if (result == GPS_NMEA_RMC || result == GPS_NMEA_GGA) {
                    print_reading(result, &reading);
                } else if (result == GPS_NMEA_INVALID) {
                    ESP_LOGW(TAG, "Frase NMEA scartata: checksum o campi non validi");
                } else if (result == GPS_NMEA_OVERFLOW) {
                    ESP_LOGW(TAG, "Frase NMEA troppo lunga: attendo il prossimo '$'");
                }
            }

            last_activity = xTaskGetTickCount();
        } else if ((TickType_t)(xTaskGetTickCount() - last_activity) >=
                   pdMS_TO_TICKS(5000)) {
            // Dopo 5 secondi senza dati segnala il problema e si prepara a una nuova ricezione.
            ESP_LOGW(TAG, "Nessun byte GPS da 5 secondi: verificare TX/RX, GND e baud");
            receiving = false;
            gps_nmea_reset(&parser);

            last_activity = xTaskGetTickCount();
        }
    }
}
