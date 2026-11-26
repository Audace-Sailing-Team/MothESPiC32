#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include <stdbool.h>
#include <stddef.h>

// Riserva lo spazio massimo per una frase GPS e il suo terminatore.
#define GPS_NMEA_MAX_LENGTH 128

// Indica se il byte completa una RMC/GGA, richiede altri dati oppure segnala un errore.
typedef enum {
    GPS_NMEA_NONE,
    GPS_NMEA_RMC,
    GPS_NMEA_GGA,
    GPS_NMEA_INVALID,
    GPS_NMEA_OVERFLOW
} gps_nmea_result_t;

// Raccoglie i dati di una singola frase; i flag has_* indicano quali campi sono presenti.
typedef struct {
    bool valid;
    double latitude_deg;
    double longitude_deg;

    bool has_speed;
    double speed_kmh;
    bool has_course;
    double course_deg;

    bool has_altitude;
    double altitude_m;
    bool has_hdop;
    double hdop;
    bool has_satellites;
    int satellites;
    int fix_quality;

    bool has_time;
    int hour, minute, second;
    bool has_date;
    int day, month, year;
} gps_reading_t;

// Conserva la frase in costruzione tra una lettura della seriale e la successiva.
typedef struct {
    char line[GPS_NMEA_MAX_LENGTH];
    size_t used;
    bool collecting;
} gps_nmea_parser_t;

// Azzera lo stato della raccolta per attendere una nuova frase.
void gps_nmea_reset(gps_nmea_parser_t *parser);

// Aggiunge un byte al parser e aggiorna reading quando riconosce una RMC o GGA.
gps_nmea_result_t gps_nmea_feed(gps_nmea_parser_t *parser,
                                unsigned char byte, gps_reading_t *reading);

#endif
