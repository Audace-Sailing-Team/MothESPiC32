// marghe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

// =========================================================================
// CONFIGURAZIONE HARDWARE UART (Collega TX GPS -> GPIO 16, RX GPS -> GPIO 17)
// =========================================================================
#define GPS_UART_NUM   UART_NUM_2
#define GPS_TX_PIN     (17)
#define GPS_RX_PIN     (16)
#define BUF_SIZE       (1024)

static const char *TAG = "GPS_NEO6M";

// Struttura dati umanamente comprensibile
typedef struct {
    bool valid;
    float latitude;       
    float longitude;      
    float speed_kmh;      
    float altitude_m;     
    int satellites;       
    int fix_quality;      
    int day, month, year; 
    int hour, min, sec;   
} gps_data_t;



static bool nmea_check_checksum(const char *sentence) {
    if (sentence[0] != '$') return false;
    const char *asterisk = strchr(sentence, '*');
    if (!asterisk || strlen(asterisk) < 3) return false;

    uint8_t checksum = 0;
    for (const char *p = sentence + 1; p < asterisk; p++) {
        checksum ^= (uint8_t)(*p);
    }

    uint8_t expected = (uint8_t)strtol(asterisk + 1, NULL, 16);
    return checksum == expected;
}

// Converte il formato NMEA (DDMM.MMMM) in Gradi Decimali standard
static float parse_nmea_coord(const char *val_str, char hemi) {
    if (!val_str || strlen(val_str) < 4) return 0.0f;
    double raw = atof(val_str);
    int deg = (int)(raw / 100.0);
    double min = raw - (deg * 100.0);
    float decimal = (float)(deg + (min / 60.0));
    if (hemi == 'S' || hemi == 'W') decimal = -decimal;
    return decimal;
}


static char *next_token(char **str_ptr) {
    if (!*str_ptr) return NULL;
    char *start = *str_ptr;
    char *comma = strpbrk(start, ",*");
    if (comma) {
        *comma = '\0';
        *str_ptr = comma + 1;
    } else {
        *str_ptr = NULL;
    }
    return start;
}


static void parse_rmc(char *line, gps_data_t *gps) {
    char *ptr = line;
    next_token(&ptr); // Salta $GPRMC

    char *time_str   = next_token(&ptr);
    char *status_str = next_token(&ptr);
    char *lat_str    = next_token(&ptr);
    char *lat_hemi   = next_token(&ptr);
    char *lon_str    = next_token(&ptr);
    char *lon_hemi   = next_token(&ptr);
    char *speed_str  = next_token(&ptr);
    next_token(&ptr); // Salta rotta (track angle)
    char *date_str   = next_token(&ptr);

    if (status_str && status_str[0] == 'A') {
        gps->valid = true;
        gps->latitude  = parse_nmea_coord(lat_str, lat_hemi ? lat_hemi[0] : 'N');
        gps->longitude = parse_nmea_coord(lon_str, lon_hemi ? lon_hemi[0] : 'E');
        gps->speed_kmh = speed_str ? (atof(speed_str) * 1.852f) : 0.0f;
    } else {
        gps->valid = false;
    }

    if (time_str && strlen(time_str) >= 6) {
        char buf[3] = {0};
        memcpy(buf, time_str, 2);     gps->hour = atoi(buf);
        memcpy(buf, time_str + 2, 2); gps->min  = atoi(buf);
        memcpy(buf, time_str + 4, 2); gps->sec  = atoi(buf);
    }

    if (date_str && strlen(date_str) >= 6) {
        char buf[3] = {0};
        memcpy(buf, date_str, 2);     gps->day   = atoi(buf);
        memcpy(buf, date_str + 2, 2); gps->month = atoi(buf);
        memcpy(buf, date_str + 4, 2); gps->year  = 2000 + atoi(buf);
    }
}


static void parse_gga(char *line, gps_data_t *gps) {
    char *ptr = line;
    next_token(&ptr); // Salta $GPGGA

    next_token(&ptr); // Ora
    next_token(&ptr); // Lat
    next_token(&ptr); // N/S
    next_token(&ptr); // Lon
    next_token(&ptr); // E/W
    char *qual_str = next_token(&ptr);
    char *sats_str = next_token(&ptr);
    next_token(&ptr); // HDOP
    char *alt_str  = next_token(&ptr);

    if (qual_str) gps->fix_quality = atoi(qual_str);
    if (sats_str) gps->satellites  = atoi(sats_str);
    if (alt_str && strlen(alt_str) > 0) gps->altitude_m = atof(alt_str);
}


void init_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
}

void gps_sampling_task(void *pvParameters) {
    uint8_t *rx_buffer = (uint8_t *)malloc(BUF_SIZE);
    char line_buf[128];
    int line_pos = 0;
    gps_data_t gps = {0};

    ESP_LOGI(TAG, "Avvio campionamento UART2 (GPIO16=RX, GPIO17=TX) a 9600 baud...");

    while (1) {
        int len = uart_read_bytes(GPS_UART_NUM, rx_buffer, BUF_SIZE - 1, pdMS_TO_TICKS(100));

        for (int i = 0; i < len; i++) {
            char c = (char)rx_buffer[i];

            if (c == '\r') continue;

            if (c == '\n') {
                line_buf[line_pos] = '\0';

                if (nmea_check_checksum(line_buf)) {
                    if (strncmp(line_buf, "$GPRMC", 6) == 0 || strncmp(line_buf, "$GNRMC", 6) == 0) {
                        parse_rmc(line_buf, &gps);

                        // Stampa il riepilogo completo ad ogni ciclo RMC (1 volta al secondo)
                        if (gps.valid) {
                            ESP_LOGI(TAG, "FIX OK | Lat: %.6f, Lon: %.6f | Alt: %.1fm | Vel: %.1f km/h | Sat: %d | %02d/%02d/%04d %02d:%02d:%02d UTC",
                                    gps.latitude, gps.longitude, gps.altitude_m,
                                    gps.speed_kmh, gps.satellites,
                                    gps.day, gps.month, gps.year,
                                    gps.hour, gps.min, gps.sec);
                        } else {
                            ESP_LOGW(TAG, "Ricerca satelliti in corso... (Satelliti visti: %d | Ora UTC: %02d:%02d:%02d)",
                                    gps.satellites, gps.hour, gps.min, gps.sec);
                        }
                    } 
                    else if (strncmp(line_buf, "$GPGGA", 6) == 0 || strncmp(line_buf, "$GNGGA", 6) == 0) {
                        parse_gga(line_buf, &gps);
                    }
                }
                line_pos = 0;
            } else {
                if (line_pos < sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                } else {
                    line_pos = 0;
                }
            }
        }
    }

    free(rx_buffer);
    vTaskDelete(NULL);
}

void app_main(void) {
    init_uart();
    xTaskCreate(gps_sampling_task, "gps_sampling_task", 4096, NULL, 5, NULL);
}