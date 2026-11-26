#include "gps_nmea.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gps_nmea_parser_t parser;
static gps_reading_t reading;
static unsigned int checks;

static void close_to(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.00000001);
}

static gps_nmea_result_t feed_text(const char *text)
{
    gps_nmea_result_t last = GPS_NMEA_NONE;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const gps_nmea_result_t result = gps_nmea_feed(&parser, *p, &reading);
        if (result != GPS_NMEA_NONE) last = result;
    }
    return last;
}

/* Calcola solo l'involucro: permette di provare campi errati con checksum giusto. */
static gps_nmea_result_t sentence(const char *body)
{
    unsigned char checksum = 0;
    for (const char *p = body; *p; ++p) checksum ^= (unsigned char)*p;
    char line[512];
    const int length = snprintf(line, sizeof(line), "$%s*%02X\r\n", body,
                                (unsigned int)checksum);
    assert(length > 0 && (size_t)length < sizeof(line));
    return feed_text(line);
}

static void expect(const char *body, gps_nmea_result_t result)
{
    gps_nmea_reset(&parser);
    assert(sentence(body) == result);
    ++checks;
}

static void test_reference_rmc(void)
{
    /* Vettore NMEA noto: checksum fisso, indipendente dal generatore di test. */
    gps_nmea_reset(&parser);
    assert(feed_text("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n")
           == GPS_NMEA_RMC);
    assert(reading.valid && reading.has_speed && reading.has_course);
    close_to(reading.latitude_deg, 48.1173);
    close_to(reading.longitude_deg, 11.516666666666667);
    close_to(reading.speed_kmh, 41.4848);
    close_to(reading.course_deg, 84.4);
    assert(reading.has_time && reading.hour == 12 && reading.minute == 35 && reading.second == 19);
    assert(reading.has_date && reading.day == 23 && reading.month == 3 && reading.year == 1994);
    assert(!reading.has_altitude && !reading.has_satellites);
    ++checks;
}

static void test_conversions(void)
{
    expect("GNRMC,120001.50,A,4539.0000,N,01346.2000,E,8.20,087.40,121126,,,A", GPS_NMEA_RMC);
    close_to(reading.latitude_deg, 45.65);
    close_to(reading.longitude_deg, 13.77);
    close_to(reading.speed_kmh, 15.1864);
    assert(reading.year == 2026 && reading.day == 12 && reading.second == 1);

    expect("GPRMC,000000,A,3456.0000,S,05822.0000,W,0,0,010100", GPS_NMEA_RMC);
    close_to(reading.latitude_deg, -34.93333333333333);
    close_to(reading.longitude_deg, -58.36666666666667);
    assert(reading.has_speed && reading.speed_kmh == 0 && reading.year == 2000);

    expect("GPGGA,120001,4539.0000,N,01346.2000,E,1,08,0.90,-2.4,M,45.0,M,,", GPS_NMEA_GGA);
    assert(reading.valid && reading.fix_quality == 1 && reading.has_satellites && reading.satellites == 8);
    assert(reading.has_altitude && reading.has_hdop && !reading.has_speed && !reading.has_date);
    close_to(reading.altitude_m, -2.4);
    close_to(reading.hdop, 0.9);

    expect("GNGGA,120002,9000.0000,S,18000.0000,W,2,12,1.0,0,M,0,M,,", GPS_NMEA_GGA);
    close_to(reading.latitude_deg, -90.0);
    close_to(reading.longitude_deg, -180.0);
    expect("GPRMC,235960,A,0000.0,N,00000.0,E,,,290224", GPS_NMEA_RMC);
    assert(reading.second == 60 && reading.day == 29 && !reading.has_speed && !reading.has_course);
}

static void test_missing_and_invalid_fix(void)
{
    expect("GPRMC,120001,A,4539.0,N,01346.2,E,8.2,87.4,121126", GPS_NMEA_RMC);
    /* Nessun vecchio dato di posizione/velocita' sopravvive al fix perso. */
    expect("GPRMC,120002,V,,,,,,,121126,,,N", GPS_NMEA_RMC);
    assert(!reading.valid && !reading.has_speed && reading.latitude_deg == 0.0);
    assert(reading.has_time && reading.second == 2 && reading.has_date);
    expect("GPGGA,120002,,,,,0,00,99.99,,M,,M,,", GPS_NMEA_GGA);
    assert(!reading.valid && reading.satellites == 0 && !reading.has_altitude);
    expect("GPRMC,120003,A,4539.0,N,01346.2,E,8.2,87.4,121126,,,N", GPS_NMEA_RMC);
    assert(!reading.valid && !reading.has_speed);
    expect("GPRMC,,V,,,,,,,", GPS_NMEA_RMC);
    assert(!reading.has_time && !reading.has_date);
    expect("GPRMC,,A,4539.0,N,01346.2,E,,,", GPS_NMEA_RMC);
    assert(reading.valid && !reading.has_time && !reading.has_date && !reading.has_speed);
    expect("GPGGA,,4539.0,N,01346.2,E,1,,,,M", GPS_NMEA_GGA);
    assert(reading.valid && !reading.has_satellites && !reading.has_altitude && !reading.has_hdop);
}

static void test_bad_fields(void)
{
    const char *bad[] = {
        "GPRMC,120001,A,4560.0,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,9000.1,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0,N,18000.1,E,8,87,121126",
        "GPRMC,120001,A,4539.0,E,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0,,01346.2,E,8,87,121126",
        "GPRMC,120001,A,,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,-4539.0,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0x,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0,N,1346.2,E,8,87,121126",
        "GPRMC,120001,A,NaN,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,00.0,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0,N,000.0,E,8,87,121126",
        "GPRMC,120001,A,4539.0,N,01346.2,E,8,87,121126,,,AA",
        "GPRMC,120001,A,4539.0,N,01346.2,E,abc,87,121126",
        "GPRMC,120001,A,4539.0,N,01346.2,E,-1,87,121126",
        "GPRMC,120001,A,4539.0,N,01346.2,E,1e2,87,121126",
        "GPRMC,120001,A,4539.0,N,01346.2,E,8,360,121126",
        "GPRMC,240000,A,4539.0,N,01346.2,E,8,87,121126",
        "GPRMC,126000,A,4539.0,N,01346.2,E,8,87,121126",
        "GPRMC,120060,A,4539.0,N,01346.2,E,8,87,121126",
        "GPRMC,120001.,A,4539.0,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0,N,01346.2,E,8,87,310226",
        "GPRMC,120001,A,4539.0,N,01346.2,E,8,87,290223",
        "GPRMC,120001,A,4539.0,N,01346.2,E,8,87,001126",
        "GPRMC,120001,X,4539.0,N,01346.2,E,8,87,121126",
        "GPRMC,120001,A,4539.0",
        "GPGGA,120001,4539.0,N,01346.2,E,,08,1,2,M",
        "GPGGA,120001,4539.0,N,01346.2,E,9,08,1,2,M",
        "GPGGA,120001,4539.0,N,01346.2,E,1,100,1,2,M",
        "GPGGA,120001,4539.0,N,01346.2,E,1,x,1,2,M",
        "GPGGA,120001,4539.0,N,01346.2,E,1,08,-1,2,M",
        "GPGGA,120001,4539.0,N,01346.2,E,1,08,1,2,F",
        "GPGGA,120001,4539.0,N,01346.2,E,1,08,1,NaN,M",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        expect(bad[i], GPS_NMEA_INVALID);
}

static void test_stream_and_checksum(void)
{
    gps_nmea_reset(&parser);
    assert(feed_text("rumore\r\n$GPRMC,120001") == GPS_NMEA_NONE);
    assert(sentence("GPRMC,120002,V,,,,,,,121126") == GPS_NMEA_RMC);
    ++checks;

    gps_nmea_reset(&parser);
    const char *first = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,";
    const char *second = "084.4,230394,003.1,W*6A\n";
    assert(feed_text(first) == GPS_NMEA_NONE);
    assert(feed_text(second) == GPS_NMEA_RMC);
    ++checks;

    const char *bad_lines[] = {
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00\n",
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6Z\n",
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6\n",
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A0\n",
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W\n",
        "$GPRMC,12\x01\n",
    };
    for (size_t i = 0; i < sizeof(bad_lines) / sizeof(bad_lines[0]); ++i) {
        gps_nmea_reset(&parser);
        assert(feed_text(bad_lines[i]) == GPS_NMEA_INVALID);
        ++checks;
    }
    gps_nmea_reset(&parser);
    assert(feed_text("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6a\n")
           == GPS_NMEA_RMC);
    ++checks;

    gps_nmea_reset(&parser);
    assert(feed_text("$incompleta") == GPS_NMEA_NONE);
    gps_nmea_reset(&parser); /* Come dopo un timeout UART. */
    assert(feed_text("coda della vecchia riga\n") == GPS_NMEA_NONE);
    assert(sentence("GPRMC,120002,V,,,,,,,121126") == GPS_NMEA_RMC);
    ++checks;

    gps_nmea_reset(&parser);
    assert(gps_nmea_feed(&parser, '$', &reading) == GPS_NMEA_NONE);
    gps_nmea_result_t result = GPS_NMEA_NONE;
    for (size_t i = 0; i < GPS_NMEA_MAX_LENGTH; ++i) {
        const gps_nmea_result_t next = gps_nmea_feed(&parser, 'X', &reading);
        if (next != GPS_NMEA_NONE) result = next;
    }
    assert(result == GPS_NMEA_OVERFLOW);
    assert(sentence("GPRMC,120002,V,,,,,,,121126") == GPS_NMEA_RMC);
    ++checks;

    expect("GPGSV,1,1,00", GPS_NMEA_NONE);
    gps_nmea_reset(&parser);
    assert(feed_text("$GPRMC,120002,V,,,,,,,121126*00\n") == GPS_NMEA_INVALID);
    assert(sentence("GPRMC,120002,V,,,,,,,121126") == GPS_NMEA_RMC);
    assert(sentence("GPGGA,120002,,,,,0,00,99.99,,M,,M,,") == GPS_NMEA_GGA);
    ++checks;
}

static int convert_file(const char *path)
{
    FILE *input = fopen(path, "rb");
    if (!input) {
        perror(path);
        return EXIT_FAILURE;
    }
    gps_nmea_reset(&parser);
    unsigned int decoded = 0, errors = 0;
    int byte;
    while ((byte = fgetc(input)) != EOF) {
        const gps_nmea_result_t result = gps_nmea_feed(&parser, (unsigned char)byte, &reading);
        if (result == GPS_NMEA_INVALID || result == GPS_NMEA_OVERFLOW) ++errors;
        if (result != GPS_NMEA_RMC && result != GPS_NMEA_GGA) continue;
        ++decoded;
        printf("%s | %s", result == GPS_NMEA_RMC ? "RMC" : "GGA",
               reading.valid ? "FIX OK" : "NESSUN FIX");
        if (reading.valid) {
            printf(" | Lat: %.6f | Lon: %.6f", reading.latitude_deg, reading.longitude_deg);
            if (reading.has_speed) printf(" | Vel: %.4f km/h", reading.speed_kmh);
            if (reading.has_altitude) printf(" | Alt: %.1f m", reading.altitude_m);
        }
        if (reading.has_satellites) printf(" | Sat usati: %d", reading.satellites);
        if (reading.has_time) printf(" | %02d:%02d:%02d UTC", reading.hour, reading.minute, reading.second);
        putchar('\n');
    }
    const bool failed = ferror(input) != 0;
    fclose(input);
    printf("Frasi convertite: %u; scartate: %u\n", decoded, errors);
    return failed || errors || !decoded ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--file") == 0) return convert_file(argv[2]);
    if (argc != 1) return EXIT_FAILURE;
    test_reference_rmc();
    test_conversions();
    test_missing_and_invalid_fix();
    test_bad_fields();
    test_stream_and_checksum();
    printf("OK: %u casi NMEA, conversioni e recupero del flusso\n", checks);
    return EXIT_SUCCESS;
}
