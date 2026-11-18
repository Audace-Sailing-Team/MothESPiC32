
// #include <stdio.h>
// #include <stdint.h>
// #include <inttypes.h>
// #include <stdbool.h>
// #include <limits.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "driver/gpio.h"
// #include "esp_timer.h"
// #include "esp_log.h"
// #include "esp_rom_sys.h"

// #define TRIG_GPIO GPIO_NUM_5
// #define ECHO_GPIO GPIO_NUM_18

// #define TRIG_PULSE_US       12
// #define TEST_DURATION_US   5000000LL

// #define ECHO_RISE_TIMEOUT_US 30000
// #define ECHO_FALL_TIMEOUT_US 30000

// #define NUM_INTERVALS 6

// static const char *TAG = "ULTRASONIC";

// // Intervalli di attesa dopo ECHO completo
// static const uint32_t intervals_ms[NUM_INTERVALS] = {
//     0, 2, 5, 10, 15, 20
// };

// typedef struct {
//     uint32_t trig;
//     uint32_t completed;
//     uint32_t timeout_rise;
//     uint32_t timeout_fall;

//     uint64_t response_total;
//     uint64_t response_min;
//     uint64_t response_max;

//     uint64_t cycle_total;
//     uint64_t cycle_min;
//     uint64_t cycle_max;
// } test_stats_t;

// static bool wait_gpio_level(gpio_num_t pin,
//                             int level,
//                             int64_t timeout_us,
//                             int64_t *timestamp)
// {
//     int64_t start = esp_timer_get_time();

//     while (gpio_get_level(pin) != level) {
//         if ((esp_timer_get_time() - start) >= timeout_us) {
//             return false;
//         }
//         esp_rom_delay_us(2);
//     }

//     *timestamp = esp_timer_get_time();
//     return true;
// }

// static void init_gpio(void)
// {
//     gpio_config_t trig_config = {
//         .pin_bit_mask = (1ULL << TRIG_GPIO),
//         .mode = GPIO_MODE_OUTPUT,
//         .pull_up_en = GPIO_PULLUP_DISABLE,
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .intr_type = GPIO_INTR_DISABLE
//     };

//     ESP_ERROR_CHECK(gpio_config(&trig_config));

//     gpio_config_t echo_config = {
//         .pin_bit_mask = (1ULL << ECHO_GPIO),
//         .mode = GPIO_MODE_INPUT,
//         .pull_up_en = GPIO_PULLUP_DISABLE,
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .intr_type = GPIO_INTR_DISABLE
//     };

//     ESP_ERROR_CHECK(gpio_config(&echo_config));

//     gpio_set_level(TRIG_GPIO, 0);
// }

// static void reset_stats(test_stats_t *s)
// {
//     s->trig = 0;
//     s->completed = 0;
//     s->timeout_rise = 0;
//     s->timeout_fall = 0;

//     s->response_total = 0;
//     s->response_min = UINT64_MAX;
//     s->response_max = 0;

//     s->cycle_total = 0;
//     s->cycle_min = UINT64_MAX;
//     s->cycle_max = 0;
// }

// static void run_interval_test(uint32_t wait_ms)
// {
//     test_stats_t s;
//     reset_stats(&s);

//     ESP_LOGI(TAG, "");
//     ESP_LOGI(TAG, "==============================================");
//     ESP_LOGI(TAG, "TEST ATTESA DOPO ECHO: %" PRIu32 " ms", wait_ms);
//     ESP_LOGI(TAG, "Durata: 5 secondi");
//     ESP_LOGI(TAG, "==============================================");

//     int64_t test_start = esp_timer_get_time();
//     int64_t test_end = test_start + TEST_DURATION_US;

//     int64_t previous_echo_fall = 0;

//     while (esp_timer_get_time() < test_end) {

//         // Assicura che ECHO sia LOW prima del nuovo TRIG.
//         if (gpio_get_level(ECHO_GPIO) != 0) {
//             int64_t t;
//             if (!wait_gpio_level(ECHO_GPIO, 0,
//                                  ECHO_FALL_TIMEOUT_US, &t)) {
//                 s.timeout_fall++;
//                 continue;
//             }
//         }

//         int64_t trig_time = esp_timer_get_time();

//         gpio_set_level(TRIG_GPIO, 1);
//         esp_rom_delay_us(TRIG_PULSE_US);
//         gpio_set_level(TRIG_GPIO, 0);

//         s.trig++;

//         int64_t rise_time = 0;

//         if (!wait_gpio_level(ECHO_GPIO, 1,
//                              ECHO_RISE_TIMEOUT_US, &rise_time)) {
//             s.timeout_rise++;

//             // Nessun ritardo aggiuntivo dopo un tentativo fallito:
//             // il timeout è già trascorso.
//             continue;
//         }

//         int64_t fall_time = 0;

//         if (!wait_gpio_level(ECHO_GPIO, 0,
//                              ECHO_FALL_TIMEOUT_US, &fall_time)) {
//             s.timeout_fall++;
//             continue;
//         }

//         s.completed++;

//         uint64_t response =
//             (uint64_t)(fall_time - trig_time);

//         s.response_total += response;

//         if (response < s.response_min)
//             s.response_min = response;

//         if (response > s.response_max)
//             s.response_max = response;

//         if (previous_echo_fall != 0) {
//             uint64_t cycle =
//                 (uint64_t)(fall_time - previous_echo_fall);

//             s.cycle_total += cycle;

//             if (cycle < s.cycle_min)
//                 s.cycle_min = cycle;

//             if (cycle > s.cycle_max)
//                 s.cycle_max = cycle;
//         }

//         previous_echo_fall = fall_time;

//         // Attesa richiesta dopo la fine dell'ECHO.
//         if (wait_ms > 0) {
//             vTaskDelay(pdMS_TO_TICKS(wait_ms));
//         }
//     }

//     int64_t duration = esp_timer_get_time() - test_start;

//     double frequency =
//         s.completed * 1000000.0 / (double)duration;

//     double success_rate =
//         s.trig > 0
//         ? 100.0 * s.completed / s.trig
//         : 0.0;

//     ESP_LOGI(TAG, "RISULTATI ATTESA %" PRIu32 " ms", wait_ms);
//     ESP_LOGI(TAG, "Durata: %.3f s", duration / 1000000.0);

//     ESP_LOGI(TAG, "TRIG: %" PRIu32, s.trig);
//     ESP_LOGI(TAG, "ECHO completi: %" PRIu32, s.completed);

//     ESP_LOGI(TAG, "Timeout salita: %" PRIu32,
//              s.timeout_rise);

//     ESP_LOGI(TAG, "Timeout discesa: %" PRIu32,
//              s.timeout_fall);

//     ESP_LOGI(TAG, "Percentuale risposta: %.2f%%",
//              success_rate);

//     ESP_LOGI(TAG, "Frequenza ECHO: %.2f misure/s",
//              frequency);

//     if (s.completed > 0) {
//         ESP_LOGI(TAG, "Risposta TRIG -> fine ECHO: media=%" PRIu64
//                  " min=%" PRIu64 " max=%" PRIu64 " us",
//                  s.response_total / s.completed,
//                  s.response_min,
//                  s.response_max);
//     }

//     if (s.completed > 1) {
//         uint32_t n = s.completed - 1;

//         ESP_LOGI(TAG, "Ciclo ECHO-ECHO: media=%" PRIu64
//                  " min=%" PRIu64 " max=%" PRIu64 " us",
//                  s.cycle_total / n,
//                  s.cycle_min,
//                  s.cycle_max);
//     }

//     ESP_LOGI(TAG, "==============================================");

//     // Pausa tra le prove per stabilizzare il sensore.
//     vTaskDelay(pdMS_TO_TICKS(1000));
// }

// void app_main(void)
// {
//     init_gpio();

//     ESP_LOGI(TAG, "Attendo stabilizzazione sensore...");
//     vTaskDelay(pdMS_TO_TICKS(1000));

//     for (int i = 0; i < NUM_INTERVALS; i++) {
//         run_interval_test(intervals_ms[i]);
//     }

//     ESP_LOGI(TAG, "TUTTI I TEST COMPLETATI");
// }

// #include <stdio.h>
// #include <stdint.h>
// #include <stdbool.h>
// #include <stdlib.h>
// #include <string.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/queue.h"

// #include "driver/gpio.h"

// #include "esp_timer.h"
// #include "esp_rom_sys.h"
// #include "esp_err.h"

// // =====================================================
// // CONFIGURAZIONE
// // =====================================================

// #define TRIG_GPIO               GPIO_NUM_5
// #define ECHO_GPIO               GPIO_NUM_18

// #define TRIG_PULSE_US           12

// #define TEST_DURATION_US        (120ULL * 1000000ULL)

// #define RISE_TIMEOUT_US         30000
// #define FALL_TIMEOUT_US         10000

// #define MAX_RECORDS             7000

// #define EDGE_QUEUE_LENGTH       16

// #define NUM_TESTS               5

// static const uint32_t rearm_delays_ms[NUM_TESTS] = {
//     0, 2, 5, 8, 10
// };

// // =====================================================
// // TIPI
// // =====================================================

// typedef enum {
//     STATUS_OK = 0,
//     STATUS_TIMEOUT_RISE,
//     STATUS_TIMEOUT_FALL
// } measurement_status_t;

// typedef struct __attribute__((packed)) {
//     uint32_t trig_us;
//     uint32_t rise_us;
//     uint32_t fall_us;
//     uint8_t status;
// } measurement_record_t;

// typedef struct {
//     int64_t timestamp_us;
//     uint8_t level;

// } echo_edge_t;

// // =====================================================
// // VARIABILI GLOBALI
// // =====================================================

// static QueueHandle_t edge_queue = NULL;

// static measurement_record_t records[MAX_RECORDS];

// static uint32_t record_count = 0;

// static int64_t test_start_us = 0;

// // =====================================================
// // ISR ECHO
// // =====================================================

// static void IRAM_ATTR echo_isr_handler(void *arg)
// {
//     BaseType_t higher_priority_task_woken = pdFALSE;

//     echo_edge_t edge;

//     edge.timestamp_us = esp_timer_get_time();
//     edge.level = gpio_get_level(ECHO_GPIO);

//     xQueueSendFromISR(
//         edge_queue,
//         &edge,
//         &higher_priority_task_woken
//     );

//     if (higher_priority_task_woken) {
//         portYIELD_FROM_ISR();
//     }
// }

// // =====================================================
// // HARDWARE INIT
// // =====================================================

// static void hardware_init(void)
// {
//     gpio_config_t trig_config = {
//         .pin_bit_mask = (1ULL << TRIG_GPIO),
//         .mode = GPIO_MODE_OUTPUT,
//         .pull_up_en = GPIO_PULLUP_DISABLE,
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .intr_type = GPIO_INTR_DISABLE
//     };

//     ESP_ERROR_CHECK(gpio_config(&trig_config));

//     gpio_config_t echo_config = {
//         .pin_bit_mask = (1ULL << ECHO_GPIO),
//         .mode = GPIO_MODE_INPUT,
//         .pull_up_en = GPIO_PULLUP_DISABLE,
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .intr_type = GPIO_INTR_ANYEDGE
//     };

//     ESP_ERROR_CHECK(gpio_config(&echo_config));

//     gpio_set_level(TRIG_GPIO, 0);

//     edge_queue = xQueueCreate(
//         EDGE_QUEUE_LENGTH,
//         sizeof(echo_edge_t)
//     );

//     if (edge_queue == NULL) {
//         printf("ERRORE: impossibile creare edge_queue\n");
//         abort();
//     }

//     ESP_ERROR_CHECK(gpio_install_isr_service(0));

//     ESP_ERROR_CHECK(
//         gpio_isr_handler_add(
//             ECHO_GPIO,
//             echo_isr_handler,
//             NULL
//         )
//     );
// }

// // =====================================================
// // TRIGGER
// // =====================================================

// static void send_trigger(void)
// {
//     gpio_set_level(TRIG_GPIO, 1);

//     esp_rom_delay_us(TRIG_PULSE_US);

//     gpio_set_level(TRIG_GPIO, 0);
// }

// // =====================================================
// // ATTESA FRONTE ECHO
// // =====================================================

// static bool wait_for_edge(
//     uint8_t expected_level,
//     uint32_t timeout_us,
//     int64_t *timestamp
// )
// {
//     int64_t start = esp_timer_get_time();

//     echo_edge_t edge;

//     while (true) {

//         int64_t elapsed =
//             esp_timer_get_time() - start;

//         if (elapsed >= timeout_us) {
//             return false;
//         }

//         int64_t remaining =
//             timeout_us - elapsed;

//         TickType_t ticks =
//             pdMS_TO_TICKS(
//                 (remaining + 999) / 1000
//             );

//         if (ticks == 0) {
//             ticks = 1;
//         }

//         if (xQueueReceive(
//                 edge_queue,
//                 &edge,
//                 ticks
//             ) == pdTRUE) {

//             if (edge.level == expected_level) {

//                 *timestamp = edge.timestamp_us;

//                 return true;
//             }
//         }
//     }
// }

// // =====================================================
// // STATISTICHE
// // =====================================================

// static int compare_u32(
//     const void *a,
//     const void *b
// )
// {
//     uint32_t x = *(const uint32_t *)a;
//     uint32_t y = *(const uint32_t *)b;

//     if (x < y) return -1;
//     if (x > y) return 1;

//     return 0;
// }

// static void print_statistics(
//     const char *name,
//     uint32_t *values,
//     uint32_t count
// )
// {
//     if (count == 0) {
//         printf("%s: nessun dato\n", name);
//         return;
//     }

//     qsort(
//         values,
//         count,
//         sizeof(uint32_t),
//         compare_u32
//     );

//     uint32_t min = values[0];
//     uint32_t max = values[count - 1];

//     uint32_t p50 = values[(count - 1) * 50 / 100];
//     uint32_t p95 = values[(count - 1) * 95 / 100];
//     uint32_t p99 = values[(count - 1) * 99 / 100];

//     printf(
//         "%s: N=%lu MIN=%lu P50=%lu P95=%lu P99=%lu MAX=%lu us\n",
//         name,
//         (unsigned long)count,
//         (unsigned long)min,
//         (unsigned long)p50,
//         (unsigned long)p95,
//         (unsigned long)p99,
//         (unsigned long)max
//     );
// }

// // =====================================================
// // STAMPA CSV
// // =====================================================

// static void print_csv(void)
// {
//     printf("\n");
//     printf("index,trig_us,rise_us,fall_us,status\n");

//     for (uint32_t i = 0; i < record_count; i++) {

//         measurement_record_t *r = &records[i];

//         printf(
//             "%lu,%lu,%lu,%lu,%u\n",

//             (unsigned long)i,
//             (unsigned long)r->trig_us,
//             (unsigned long)r->rise_us,
//             (unsigned long)r->fall_us,
//             (unsigned int)r->status
//         );
//     }
// }

// // =====================================================
// // ANALISI DELLA PROVA
// // =====================================================

// static void analyze_test(void)
// {
//     static uint32_t scratch[MAX_RECORDS];

//     uint32_t successful = 0;
//     uint32_t timeout_rise = 0;
//     uint32_t timeout_fall = 0;

//     uint32_t n = 0;

//     printf("\n========== RISULTATI ==========\n");

//     printf("Tentativi: %lu\n",
//            (unsigned long)record_count);

//     // Successi e timeout

//     for (uint32_t i = 0; i < record_count; i++) {

//         switch (records[i].status) {

//             case STATUS_OK:
//                 successful++;
//                 break;

//             case STATUS_TIMEOUT_RISE:
//                 timeout_rise++;
//                 break;

//             case STATUS_TIMEOUT_FALL:
//                 timeout_fall++;
//                 break;
//         }
//     }

//     printf("Successi: %lu\n",
//            (unsigned long)successful);

//     printf("Timeout salita: %lu\n",
//            (unsigned long)timeout_rise);

//     printf("Timeout discesa: %lu\n",
//            (unsigned long)timeout_fall);

//     if (record_count > 0) {

//         printf("Success rate: %.2f %%\n",
//                100.0f * successful / record_count);
//     }

//     // TRIG interval

//     n = 0;

//     for (uint32_t i = 1; i < record_count; i++) {

//         scratch[n++] =
//             records[i].trig_us -
//             records[i - 1].trig_us;
//     }

//     print_statistics("TRIG interval", scratch, n);

//     // TRIG -> ECHO rise

//     n = 0;

//     for (uint32_t i = 0; i < record_count; i++) {

//         if (records[i].status == STATUS_OK) {

//             scratch[n++] =
//                 records[i].rise_us -
//                 records[i].trig_us;
//         }
//     }

//     print_statistics("TRIG -> ECHO rise", scratch, n);

//     // ECHO HIGH

//     n = 0;

//     for (uint32_t i = 0; i < record_count; i++) {

//         if (records[i].status == STATUS_OK) {

//             scratch[n++] =
//                 records[i].fall_us -
//                 records[i].rise_us;
//         }
//     }

//     print_statistics("ECHO HIGH", scratch, n);

//     // ECHO fall -> next TRIG

//     n = 0;

//     for (uint32_t i = 0; i + 1 < record_count; i++) {

//         if (records[i].status == STATUS_OK) {

//             scratch[n++] =
//                 records[i + 1].trig_us -
//                 records[i].fall_us;
//         }
//     }

//     print_statistics("ECHO fall -> next TRIG", scratch, n);
// }

// // =====================================================
// // ESECUZIONE SINGOLO TEST
// // =====================================================

// static void run_test(uint32_t rearm_delay_ms)
// {
//     printf("\n\n");
//     printf("====================================\n");
//     printf("TEST RIARMO: %lu ms\n",
//            (unsigned long)rearm_delay_ms);
//     printf("DURATA: 120 secondi\n");
//     printf("====================================\n");

//     record_count = 0;

//     while (xQueueReceive(
//         edge_queue,
//         &(echo_edge_t){0},
//         0
//     ) == pdTRUE) {
//         // Svuota eventuali eventi precedenti
//     }

//     test_start_us = esp_timer_get_time();

//     int64_t test_end_us =
//         test_start_us + TEST_DURATION_US;

//     int64_t previous_trig = 0;

//     while (
//         esp_timer_get_time() < test_end_us &&
//         record_count < MAX_RECORDS
//     ) {

//         measurement_record_t *r =
//             &records[record_count];

//         memset(r, 0, sizeof(*r));

//         int64_t trig = esp_timer_get_time();

//         r->trig_us =
//             (uint32_t)(trig - test_start_us);

//         previous_trig = trig;

//         send_trigger();

//         int64_t rise = 0;
//         int64_t fall = 0;

//         bool rise_ok = wait_for_edge(
//             1,
//             RISE_TIMEOUT_US,
//             &rise
//         );

//         if (!rise_ok) {

//             r->status = STATUS_TIMEOUT_RISE;

//             record_count++;

//             continue;
//         }

//         r->rise_us =
//             (uint32_t)(rise - test_start_us);

//         bool fall_ok = wait_for_edge(
//             0,
//             FALL_TIMEOUT_US,
//             &fall
//         );

//         if (!fall_ok) {

//             r->status = STATUS_TIMEOUT_FALL;

//             record_count++;

//             continue;
//         }

//         r->fall_us =
//             (uint32_t)(fall - test_start_us);

//         r->status = STATUS_OK;

//         record_count++;

//         if (rearm_delay_ms > 0) {

//             esp_rom_delay_us(
//                 rearm_delay_ms * 1000
//             );
//         }
//     }

//     printf("\nTEST TERMINATO\n");

//     analyze_test();

//     printf("\n========== CSV RAW ==========\n");

//     print_csv();

//     printf("\nFINE TEST RIARMO %lu ms\n",
//            (unsigned long)rearm_delay_ms);
// }

// // =====================================================
// // MAIN TASK
// // =====================================================

// void app_main(void)
// {
//     printf("\n");
//     printf("AJ-SR04M CHARACTERIZATION\n");
//     printf("ESP32-WROOM-32\n");
//     printf("ESP-IDF 5.5\n");

//     hardware_init();

//     vTaskDelay(pdMS_TO_TICKS(1000));

//     for (uint32_t i = 0; i < NUM_TESTS; i++) {

//         run_test(rearm_delays_ms[i]);

//         printf("\nAttesa 3 secondi...\n");

//         vTaskDelay(pdMS_TO_TICKS(3000));
//     }

//     printf("\n");
//     printf("TUTTI I TEST COMPLETATI\n");
// }



// // #include <stdio.h>
// // #include <stdint.h>
// // #include <stdbool.h>

// // #include "freertos/FreeRTOS.h"
// // #include "freertos/task.h"

// // #include "driver/gpio.h"
// // #include "esp_timer.h"
// // #include "esp_rom_sys.h"
// // #include "esp_log.h"
// // #include "esp_err.h"

// // // ============================================================
// // // CONFIGURAZIONE
// // // ============================================================

// // #define TRIG_PIN GPIO_NUM_5
// // #define ECHO_PIN GPIO_NUM_18

// // #define SAMPLE_RATE_HZ          5
// // #define SAMPLE_PERIOD_MS        (1000 / SAMPLE_RATE_HZ)

// // #define TRIGGER_PULSE_US        10
// // #define ACQUISITION_TIMEOUT_US  45000

// // #define STATS_SAMPLES           5

// // #define MIN_DISTANCE_MM         1
// // #define MAX_DISTANCE_MM         200

// // #define RING_BUFFER_SIZE        32

// // static const char *TAG = "ULTRASONIC";

// // // ============================================================
// // // RISULTATI
// // // ============================================================

// // typedef enum
// // {
// //     STATUS_NO_ECHO = 0,
// //     STATUS_ECHO_STUCK_HIGH,
// //     STATUS_OUT_OF_RANGE,
// //     STATUS_VALID
// // } measurement_status_t;

// // typedef struct
// // {
// //     int64_t timestamp_us;
// //     uint32_t echo_us;
// //     uint32_t distance_mm;
// //     measurement_status_t status;
// // } measurement_t;

// // // ============================================================
// // // RING BUFFER
// // // ============================================================

// // static measurement_t measurement_ring[RING_BUFFER_SIZE];

// // static uint32_t ring_head = 0;
// // static uint32_t ring_count = 0;

// // static void ring_store(const measurement_t *record)
// // {
// //     measurement_ring[ring_head] = *record;

// //     ring_head = (ring_head + 1) % RING_BUFFER_SIZE;

// //     if (ring_count < RING_BUFFER_SIZE)
// //         ring_count++;
// // }

// // // ============================================================
// // // STATISTICHE
// // // ============================================================

// // static uint32_t attempts = 0;
// // static uint32_t triggers = 0;
// // static uint32_t valid = 0;
// // static uint32_t no_echo = 0;
// // static uint32_t stuck_high = 0;
// // static uint32_t out_of_range = 0;

// // static uint32_t last_echo_us = 0;
// // static uint32_t last_distance_mm = 0;

// // // ============================================================
// // // GPIO
// // // ============================================================

// // static void init_gpio(void)
// // {
// //     gpio_config_t trig = {
// //         .pin_bit_mask = 1ULL << TRIG_PIN,
// //         .mode = GPIO_MODE_OUTPUT,
// //         .pull_up_en = GPIO_PULLUP_DISABLE,
// //         .pull_down_en = GPIO_PULLDOWN_DISABLE,
// //         .intr_type = GPIO_INTR_DISABLE
// //     };

// //     gpio_config_t echo = {
// //         .pin_bit_mask = 1ULL << ECHO_PIN,
// //         .mode = GPIO_MODE_INPUT,
// //         .pull_up_en = GPIO_PULLUP_DISABLE,
// //         .pull_down_en = GPIO_PULLDOWN_ENABLE,
// //         .intr_type = GPIO_INTR_DISABLE
// //     };

// //     ESP_ERROR_CHECK(gpio_config(&trig));
// //     ESP_ERROR_CHECK(gpio_config(&echo));

// //     ESP_ERROR_CHECK(gpio_set_level(TRIG_PIN, 0));
// // }

// // // ============================================================
// // // ACQUISIZIONE ECHO SENZA RMT
// // // ============================================================

// // static measurement_status_t read_echo(
// //     uint32_t *echo_duration_us)
// // {
// //     *echo_duration_us = 0;

// //     // Genera impulso TRIG.
// //     gpio_set_level(TRIG_PIN, 1);
// //     esp_rom_delay_us(TRIGGER_PULSE_US);
// //     gpio_set_level(TRIG_PIN, 0);

// //     triggers++;

// //     // Timeout assoluto a partire dal trigger.
// //     int64_t deadline =
// //         esp_timer_get_time() + ACQUISITION_TIMEOUT_US;

// //     // --------------------------------------------------------
// //     // Attende il fronte di salita ECHO.
// //     // --------------------------------------------------------

// //     while (gpio_get_level(ECHO_PIN) == 0)
// //     {
// //         if (esp_timer_get_time() >= deadline)
// //         {
// //             return STATUS_NO_ECHO;
// //         }

// //         esp_rom_delay_us(1);
// //     }

// //     // Inizio impulso HIGH.
// //     int64_t echo_start = esp_timer_get_time();

// //     // --------------------------------------------------------
// //     // Attende il fronte di discesa ECHO.
// //     // --------------------------------------------------------

// //     while (gpio_get_level(ECHO_PIN) == 1)
// //     {
// //         if (esp_timer_get_time() >= deadline)
// //         {
// //             return STATUS_ECHO_STUCK_HIGH;
// //         }

// //         esp_rom_delay_us(1);
// //     }

// //     int64_t echo_end = esp_timer_get_time();

// //     *echo_duration_us =
// //         (uint32_t)(echo_end - echo_start);

// //     return STATUS_VALID;
// // }

// // // ============================================================
// // // STATISTICHE
// // // ============================================================

// // static void print_statistics(void)
// // {
// //     ESP_LOGI(TAG,
// //         "N=%lu | trig=%lu | valid=%lu | no_echo=%lu | stuck=%lu | out_range=%lu",
// //         (unsigned long)attempts,
// //         (unsigned long)triggers,
// //         (unsigned long)valid,
// //         (unsigned long)no_echo,
// //         (unsigned long)stuck_high,
// //         (unsigned long)out_of_range);

// //     ESP_LOGI(TAG,
// //         "Ultimo ECHO=%lu us | distanza=%lu mm | ring=%lu/%u",
// //         (unsigned long)last_echo_us,
// //         (unsigned long)last_distance_mm,
// //         (unsigned long)ring_count,
// //         (unsigned int)RING_BUFFER_SIZE);

// //     attempts = 0;
// //     triggers = 0;
// //     valid = 0;
// //     no_echo = 0;
// //     stuck_high = 0;
// //     out_of_range = 0;
// // }

// // // ============================================================
// // // TASK PRINCIPALE
// // // ============================================================

// // static void acquisition_loop(void)
// // {
// //     measurement_t record = {0};

// //     TickType_t last_wake = xTaskGetTickCount();

// //     while (1)
// //     {
// //         attempts++;

// //         record = (measurement_t){0};
// //         record.timestamp_us = esp_timer_get_time();

// //         measurement_status_t status =
// //             read_echo(&record.echo_us);

// //         record.status = status;

// //         last_echo_us = record.echo_us;
// //         last_distance_mm = 0;

// //         if (status == STATUS_NO_ECHO)
// //         {
// //             no_echo++;
// //         }
// //         else if (status == STATUS_ECHO_STUCK_HIGH)
// //         {
// //             stuck_high++;
// //         }
// //         else
// //         {
// //             // Distanza in mm:
// //             // tempo_us * 343 / 2000

// //             record.distance_mm =
// //                 (uint32_t)(((uint64_t)record.echo_us * 343ULL)
// //                            / 2000ULL);

// //             last_distance_mm = record.distance_mm;

// //             record.status = STATUS_VALID;
// //             valid++;
// //         }

// //         ring_store(&record);

// //         ESP_LOGI(TAG,
// //     "ECHO: %lu us | DISTANZA: %lu mm | STATUS: %d",
// //     (unsigned long)record.echo_us,
// //     (unsigned long)record.distance_mm,
// //     record.status);

// //         if (attempts >= STATS_SAMPLES)
// //         {
// //             print_statistics();
// //         }

// //         // Mantiene il periodo nominale di 20 ms.
// //         vTaskDelayUntil(
// //             &last_wake,
// //             pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
// //     }
// // }

// // // ============================================================
// // // APP MAIN
// // // ============================================================

// // void app_main(void)
// // {
// //     init_gpio();

// //     ESP_LOGI(TAG, "GPIO polling mode - RMT disabilitato");
// //     ESP_LOGI(TAG, "TRIG GPIO%d | ECHO GPIO%d",
// //              TRIG_PIN, ECHO_PIN);

// //     ESP_LOGI(TAG, "Frequenza nominale: %d Hz",
// //              SAMPLE_RATE_HZ);

// //     acquisition_loop();
// // }



// // //TEST 200 HZ

// // // #include <stdio.h>
// // // #include <stdint.h>
// // // #include <stdbool.h>

// // // #include "freertos/FreeRTOS.h"
// // // #include "freertos/task.h"

// // // #include "driver/gpio.h"
// // // #include "esp_timer.h"
// // // #include "esp_rom_sys.h"
// // // #include "esp_log.h"
// // // #include "esp_err.h"
// // // #include "esp_task_wdt.h"

// // // // ============================================================
// // // // CONFIGURAZIONE TEST 
// // // // ============================================================

// // // #define TRIG_PIN                GPIO_NUM_5
// // // #define ECHO_PIN                GPIO_NUM_18

// // // // Nota: 200 Hz (5ms) sono estremi per HC-SR04. Se vedi molti NoEcho, 
// // // // prova a impostare SAMPLE_RATE_HZ a 20 o 50 Hz.
// // // #define SAMPLE_RATE_HZ          200
// // // #define SAMPLE_PERIOD_US        (1000000 / SAMPLE_RATE_HZ) 

// // // #define TRIGGER_PULSE_US        15
// // // #define ACQUISITION_TIMEOUT_US  4500

// // // #define STATS_SAMPLES           200
// // // #define STUCK_HIGH_RECOVERY_LIMIT 10  

// // // #define MIN_DISTANCE_MM         200
// // // #define MAX_DISTANCE_MM         600

// // // static const char *TAG = "ULTRASONIC";

// // // // ============================================================
// // // // STATI
// // // // ============================================================

// // // typedef enum
// // // {
// // //     STATUS_NO_ECHO = 0,
// // //     STATUS_STUCK_HIGH,
// // //     STATUS_OUT_OF_RANGE,
// // //     STATUS_VALID
// // // } measurement_status_t;

// // // // ============================================================
// // // // STATISTICHE
// // // // ============================================================

// // // static uint32_t attempts = 0;
// // // static uint32_t valid = 0;
// // // static uint32_t no_echo = 0;
// // // static uint32_t stuck_high = 0;
// // // static uint32_t out_of_range = 0;
// // // static uint32_t missed_periods = 0;
// // // static uint32_t consecutive_stuck = 0;

// // // static uint32_t last_echo_us = 0;
// // // static uint32_t last_distance_mm = 0;

// // // static uint32_t min_period_us = UINT32_MAX;
// // // static uint32_t max_period_us = 0;
// // // static uint64_t sum_period_us = 0;

// // // static uint32_t min_echo_us = UINT32_MAX;
// // // static uint32_t max_echo_us = 0;

// // // static int64_t previous_trigger_us = 0;

// // // // ============================================================
// // // // GPIO & RECOVERY
// // // // ============================================================

// // // static void init_gpio(void)
// // // {
// // //     gpio_config_t trig = {
// // //         .pin_bit_mask = 1ULL << TRIG_PIN,
// // //         .mode = GPIO_MODE_OUTPUT,
// // //         .pull_up_en = GPIO_PULLUP_DISABLE,
// // //         .pull_down_en = GPIO_PULLDOWN_DISABLE,
// // //         .intr_type = GPIO_INTR_DISABLE
// // //     };

// // //     gpio_config_t echo = {
// // //         .pin_bit_mask = 1ULL << ECHO_PIN,
// // //         .mode = GPIO_MODE_INPUT,
// // //         .pull_up_en = GPIO_PULLUP_DISABLE,
// // //         .pull_down_en = GPIO_PULLDOWN_ENABLE,
// // //         .intr_type = GPIO_INTR_DISABLE
// // //     };

// // //     ESP_ERROR_CHECK(gpio_config(&trig));
// // //     ESP_ERROR_CHECK(gpio_config(&echo));
// // //     ESP_ERROR_CHECK(gpio_set_level(TRIG_PIN, 0));
// // // }

// // // static void reset_echo_pin_state(void)
// // // {
// // //     ESP_LOGW(TAG, "Tentativo di reset hardware/software del pin ECHO...");
// // //     gpio_set_direction(ECHO_PIN, GPIO_MODE_OUTPUT);
// // //     gpio_set_level(ECHO_PIN, 0);
// // //     esp_rom_delay_us(50);
// // //     gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
// // // }

// // // // ============================================================
// // // // ACQUISIZIONE ECHO
// // // // ============================================================

// // // static measurement_status_t read_echo(
// // //     uint32_t *echo_duration_us,
// // //     uint32_t *trigger_period_us)
// // // {
// // //     *echo_duration_us = 0;
// // //     *trigger_period_us = 0;

// // //     if (gpio_get_level(ECHO_PIN) == 1)
// // //     {
// // //         return STATUS_STUCK_HIGH;
// // //     }

// // //     // Genera TRIG.
// // //     gpio_set_level(TRIG_PIN, 1);
// // //     esp_rom_delay_us(TRIGGER_PULSE_US);
// // //     gpio_set_level(TRIG_PIN, 0);

// // //     int64_t trigger_time = esp_timer_get_time();

// // //     if (previous_trigger_us != 0)
// // //     {
// // //         *trigger_period_us = (uint32_t)(trigger_time - previous_trigger_us);
// // //     }
// // //     previous_trigger_us = trigger_time;

// // //     int64_t deadline = trigger_time + ACQUISITION_TIMEOUT_US;

// // //     while (gpio_get_level(ECHO_PIN) == 0)
// // //     {
// // //         if (esp_timer_get_time() >= deadline)
// // //             return STATUS_NO_ECHO;
// // //     }

// // //     int64_t echo_start = esp_timer_get_time();

// // //     while (gpio_get_level(ECHO_PIN) == 1)
// // //     {
// // //         if (esp_timer_get_time() >= deadline)
// // //             return STATUS_STUCK_HIGH;
// // //     }

// // //     int64_t echo_end = esp_timer_get_time();
// // //     *echo_duration_us = (uint32_t)(echo_end - echo_start);

// // //     return STATUS_VALID;
// // // }

// // // // ============================================================
// // // // STATISTICHE
// // // // ============================================================

// // // static void print_statistics(void)
// // // {
// // //     uint32_t received = valid + out_of_range;
// // //     uint32_t received_total = received + stuck_high;

// // //     uint32_t average_period = attempts > 1 ? (uint32_t)(sum_period_us / (attempts - 1)) : 0;
// // //     float actual_hz = average_period > 0 ? 1000000.0f / average_period : 0.0f;
// // //     float valid_percent = attempts > 0 ? (100.0f * valid / attempts) : 0.0f;
// // //     float echo_percent = attempts > 0 ? (100.0f * received_total / attempts) : 0.0f;

// // //     ESP_LOGI(TAG, "Tentativi=%lu | validi=%lu (%.1f%%) | ECHO=%.1f%%",
// // //              (unsigned long)attempts, (unsigned long)valid, valid_percent, echo_percent);

// // //     ESP_LOGI(TAG, "NoEcho=%lu | Stuck=%lu | FuoriRange=%lu | Periodi saltati=%lu",
// // //              (unsigned long)no_echo, (unsigned long)stuck_high,
// // //              (unsigned long)out_of_range, (unsigned long)missed_periods);

// // //     ESP_LOGI(TAG, "TRIG: medio=%lu us | min=%lu us | max=%lu us | frequenza=%.1f Hz",
// // //              (unsigned long)average_period, (unsigned long)min_period_us,
// // //              (unsigned long)max_period_us, actual_hz);

// // //     ESP_LOGI(TAG, "Ultimo ECHO=%lu us | distanza=%lu mm",
// // //              (unsigned long)last_echo_us, (unsigned long)last_distance_mm);

// // //     if (min_echo_us != UINT32_MAX)
// // //     {
// // //         ESP_LOGI(TAG, "Durata ECHO: min=%lu us | max=%lu us",
// // //                  (unsigned long)min_echo_us, (unsigned long)max_echo_us);
// // //     }

// // //     ESP_LOGI(TAG, "--------------------------------");

// // //     attempts = 0;
// // //     valid = 0;
// // //     no_echo = 0;
// // //     stuck_high = 0;
// // //     out_of_range = 0;
// // //     missed_periods = 0;
// // //     min_period_us = UINT32_MAX;
// // //     max_period_us = 0;
// // //     sum_period_us = 0;
// // //     min_echo_us = UINT32_MAX;
// // //     max_echo_us = 0;
// // // }

// // // // ============================================================
// // // // TASK DEDICATO DI ACQUISIZIONE
// // // // ============================================================

// // // static void ultrasonic_task(void *pvParameters)
// // // {
// // //     // Registra questo task nel Task Watchdog Timer
// // //     ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

// // //     int64_t next_trigger_us = esp_timer_get_time();

// // //     while (1)
// // //     {
// // //         int64_t now = esp_timer_get_time();

// // //         if (now < next_trigger_us)
// // //         {
// // //             int64_t remaining = next_trigger_us - now;
// // //             if (remaining > 2000)
// // //             {
// // //                 vTaskDelay(pdMS_TO_TICKS(1));
// // //             }
// // //             else if (remaining > 100)
// // //             {
// // //                 esp_rom_delay_us(remaining - 50);
// // //             }
            
// // //             while (esp_timer_get_time() < next_trigger_us) {
// // //                 // Attesa attiva fine
// // //             }
// // //         }

// // //         uint32_t echo_us = 0;
// // //         uint32_t period_us = 0;

// // //         measurement_status_t status = read_echo(&echo_us, &period_us);
// // //         attempts++;

// // //         last_echo_us = echo_us;
// // //         last_distance_mm = 0;

// // //         if (status == STATUS_STUCK_HIGH)
// // //         {
// // //             stuck_high++;
// // //             consecutive_stuck++;
// // //             if (consecutive_stuck >= STUCK_HIGH_RECOVERY_LIMIT)
// // //             {
// // //                 reset_echo_pin_state();
// // //                 consecutive_stuck = 0;
// // //             }
// // //         }
// // //         else
// // //         {
// // //             consecutive_stuck = 0;
// // //         }

// // //         if (period_us > 0)
// // //         {
// // //             if (period_us < min_period_us) min_period_us = period_us;
// // //             if (period_us > max_period_us) max_period_us = period_us;
// // //             sum_period_us += period_us;

// // //             if (period_us > SAMPLE_PERIOD_US + 200)
// // //                 missed_periods++;
// // //         }

// // //         if (status == STATUS_NO_ECHO)
// // //         {
// // //             no_echo++;
// // //         }
// // //         else if (status == STATUS_VALID)
// // //         {
// // //             if (echo_us < min_echo_us) min_echo_us = echo_us;
// // //             if (echo_us > max_echo_us) max_echo_us = echo_us;

// // //             uint32_t distance_mm = (uint32_t)(((uint64_t)echo_us * 343ULL) / 2000ULL);
// // //             last_distance_mm = distance_mm;

// // //             if (distance_mm < MIN_DISTANCE_MM || distance_mm > MAX_DISTANCE_MM)
// // //             {
// // //                 out_of_range++;
// // //             }
// // //             else
// // //             {
// // //                 valid++;
// // //             }
// // //         }

// // //         next_trigger_us += SAMPLE_PERIOD_US;
// // //         now = esp_timer_get_time();
        
// // //         if (now >= next_trigger_us)
// // //         {
// // //             next_trigger_us = now + SAMPLE_PERIOD_US;
// // //         }

// // //         if (attempts >= STATS_SAMPLES)
// // //         {
// // //             print_statistics();
// // //         }

// // //         // Nutre il watchdog a ogni singola iterazione per evitare timeout su IDLE1
// // //         esp_task_wdt_reset();
// // //     }
// // // }

// // // // ============================================================
// // // // APP MAIN
// // // // ============================================================

// // // void app_main(void)
// // // {
// // //     init_gpio();

// // //     ESP_LOGI(TAG, "====================================");
// // //     ESP_LOGI(TAG, "TEST FREQUENZA ULTRASONICO (STABILE)");
// // //     ESP_LOGI(TAG, "Frequenza richiesta: %d Hz", SAMPLE_RATE_HZ);
// // //     ESP_LOGI(TAG, "Periodo richiesto: %d us", SAMPLE_PERIOD_US);
// // //     ESP_LOGI(TAG, "Timeout ECHO: %d us", ACQUISITION_TIMEOUT_US);
// // //     ESP_LOGI(TAG, "TRIG GPIO%d | ECHO GPIO%d", TRIG_PIN, ECHO_PIN);
// // //     ESP_LOGI(TAG, "====================================");

// // //     // Priorità abbassata a 1 (invece di 5) per evitare di bloccare il task IDLE della CPU 1
// // //     xTaskCreatePinnedToCore(
// // //         ultrasonic_task,
// // //         "ultrasonic_task",
// // //         4096,
// // //         NULL,
// // //         1, 
// // //         NULL,
// // //         1
// // //     );
// // // }