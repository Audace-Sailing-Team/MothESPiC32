#include "gps_nmea.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Controlla se il carattere ricevuto e' una cifra da 0 a 9.
static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

// Converte una cifra del checksum da esadecimale a numero; restituisce -1 se non e' valida.
static int hex_digit(char c)
{
    if (digit(c)) return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Controlla il testo e lo converte in un numero decimale, rifiutando campi vuoti o malformati.
static bool decimal(const char *text, bool allow_sign, double *value)
{
    const char *p = text;
    if (allow_sign && (*p == '-' || *p == '+')) ++p;
    if (!digit(*p)) return false;
    while (digit(*p)) ++p;
    if (*p == '.') {
        ++p;
        if (!digit(*p)) return false;
        while (digit(*p)) ++p;
    }
    if (*p != '\0') return false;

    // Esegue la conversione e verifica che il valore sia finito e rappresentabile.
    errno = 0;
    char *end;
    const double parsed = strtod(text, &end);
    if (errno == ERANGE || *end != '\0' || !isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

// Legge un numero intero e controlla che non superi il limite previsto per quel campo.
static bool integer(const char *text, int maximum, int *value)
{
    if (*text == '\0') return false;
    int parsed = 0;
    for (const char *p = text; *p; ++p) {
        if (!digit(*p) || parsed > maximum / 10) return false;
        parsed = parsed * 10 + *p - '0';
        if (parsed > maximum) return false;
    }
    *value = parsed;
    return true;
}

// Legge una coordinata GPS e il suo emisfero per ottenere latitudine o longitudine in gradi decimali.
static bool coordinate(const char *text, const char *hemisphere,
                        bool latitude, double *degrees)
{
    // Verifica che il formato abbia le cifre previste e un emisfero adatto alla coordinata.
    const size_t width = latitude ? 4 : 5;
    const size_t length = strlen(text);
    if (length < width || (length > width && text[width] != '.')) return false;
    for (size_t i = 0; i < width; ++i) if (!digit(text[i])) return false;
    if (strlen(hemisphere) != 1) return false;
    const char hemi = hemisphere[0];
    if (latitude ? (hemi != 'N' && hemi != 'S') :
                   (hemi != 'E' && hemi != 'W')) return false;
    double raw;
    if (!decimal(text, false, &raw)) return false;

    // Separa i gradi dai minuti: per esempio 4539.0000 contiene 45 gradi e 39 minuti.
    const int whole_degrees = (int)((text[0] - '0') * 10 + text[1] - '0');
    const int deg = latitude ? whole_degrees :
                    whole_degrees * 10 + text[2] - '0';
    const double minutes = raw - deg * 100.0;

    // Scarta minuti da 60 in su e coordinate oltre 90 gradi di latitudine o 180 di longitudine.
    const int maximum = latitude ? 90 : 180;
    if (minutes < 0.0 || minutes >= 60.0 || deg > maximum ||
        (deg == maximum && minutes != 0.0)) return false;

    // Converte i minuti in frazioni di grado e applica il segno negativo per Sud e Ovest.
    *degrees = deg + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') *degrees = -*degrees;
    return true;
}

// Unisce due cifre di testo nel numero corrispondente, per esempio 2 e 6 diventano 26.
static int two_digits(const char *p)
{
    return (p[0] - '0') * 10 + p[1] - '0';
}

// Estrae ore, minuti e secondi dall'ora GPS e ne controlla i valori.
static bool utc_time(const char *text, gps_reading_t *reading)
{
    if (*text == '\0') return true;
    const size_t length = strlen(text);
    if (length < 6) return false;
    for (size_t i = 0; i < 6; ++i) if (!digit(text[i])) return false;
    if (length > 6) {
        if (text[6] != '.' || length == 7) return false;
        for (size_t i = 7; i < length; ++i) if (!digit(text[i])) return false;
    }
    reading->hour = two_digits(text);
    reading->minute = two_digits(text + 2);
    reading->second = two_digits(text + 4);

    if (reading->hour > 23 || reading->minute > 59 || reading->second > 60 ||
        (reading->second == 60 &&
         (reading->hour != 23 || reading->minute != 59))) return false;
    reading->has_time = true;
    return true;
}

// Estrae giorno, mese e anno dalla data GPS e controlla che la data esista.
static bool utc_date(const char *text, gps_reading_t *reading)
{
    if (*text == '\0') return true;
    if (strlen(text) != 6) return false;
    for (size_t i = 0; i < 6; ++i) if (!digit(text[i])) return false;
    reading->day = two_digits(text);
    reading->month = two_digits(text + 2);
    const int year = two_digits(text + 4);

    // Interpreta l'anno a due cifre nell'intervallo 1980-2079.
    reading->year = year >= 80 ? 1900 + year : 2000 + year;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (reading->month < 1 || reading->month > 12) return false;
    int maximum = days[reading->month - 1];
    // Aggiunge il 29 febbraio soltanto negli anni bisestili.
    if (reading->month == 2 && reading->year % 4 == 0 &&
        (reading->year % 100 != 0 || reading->year % 400 == 0)) ++maximum;
    if (reading->day < 1 || reading->day > maximum) return false;
    reading->has_date = true;
    return true;
}

// Distingue un campo numerico assente da uno presente e controlla il numero quando c'e'.
static bool optional_number(const char *text, bool allow_sign,
                             bool *present, double *value)
{
    *present = *text != '\0';
    return !*present || decimal(text, allow_sign, value);
}

// Estrae dalla frase RMC posizione, velocita', rotta, data e ora.
static bool parse_rmc(char **fields, size_t count, gps_reading_t *reading)
{
    if (count < 10 || strlen(fields[2]) != 1 ||
        (fields[2][0] != 'A' && fields[2][0] != 'V')) return false;
    if (!utc_time(fields[1], reading) || !utc_date(fields[9], reading)) return false;
    reading->valid = fields[2][0] == 'A';

    // Controlla anche il campo mode, se presente: N indica dati non validi.
    if (count > 12 && fields[12][0] != '\0') {
        if (strlen(fields[12]) != 1 || fields[12][0] < 'A' || fields[12][0] > 'Z')
            return false;
        if (fields[12][0] == 'N') reading->valid = false;
    }

    if (!reading->valid) return true;
    if (!coordinate(fields[3], fields[4], true, &reading->latitude_deg) ||
        !coordinate(fields[5], fields[6], false, &reading->longitude_deg)) return false;
    // Legge velocita' e rotta soltanto dopo aver riconosciuto una posizione valida.
    double knots = 0.0;
    if (!optional_number(fields[7], false, &reading->has_speed, &knots) ||
        !optional_number(fields[8], false, &reading->has_course,
                         &reading->course_deg)) return false;

    // Converte la velocita' dai nodi ai km/h: un nodo corrisponde a 1.852 km/h.
    reading->speed_kmh = knots * 1.852;
    if (!isfinite(reading->speed_kmh) ||
        (reading->has_course && reading->course_deg >= 360.0)) return false;
    return true;
}

// Estrae dalla frase GGA posizione, quota, satelliti usati e qualita' del fix.
static bool parse_gga(char **fields, size_t count, gps_reading_t *reading)
{
    if (count < 11 || !utc_time(fields[1], reading) ||
        !integer(fields[6], 8, &reading->fix_quality)) return false;
    reading->has_satellites = fields[7][0] != '\0';
    if (reading->has_satellites && !integer(fields[7], 99, &reading->satellites))
        return false;
    reading->valid = reading->fix_quality > 0;
    if (!reading->valid) return true;
    if (!coordinate(fields[2], fields[3], true, &reading->latitude_deg) ||
        !coordinate(fields[4], fields[5], false, &reading->longitude_deg) ||
        !optional_number(fields[8], false, &reading->has_hdop, &reading->hdop) ||
        !optional_number(fields[9], true, &reading->has_altitude,
                         &reading->altitude_m)) return false;

    // Accetta la quota solo se l'unita' indicata nella frase e' il metro.
    return !reading->has_altitude || strcmp(fields[10], "M") == 0;
}

// Verifica una frase completa, ne separa i campi e sceglie come interpretarli.
static gps_nmea_result_t parse_line(char *line, gps_reading_t *reading)
{
    // Confronta il checksum calcolato sul messaggio con quello ricevuto dopo l'asterisco.
    char *asterisk = strchr(line, '*');
    if (!asterisk || strlen(asterisk) != 3) return GPS_NMEA_INVALID;
    const int high = hex_digit(asterisk[1]);
    const int low = hex_digit(asterisk[2]);
    if (high < 0 || low < 0) return GPS_NMEA_INVALID;
    unsigned char checksum = 0;
    for (const char *p = line + 1; p < asterisk; ++p) checksum ^= (unsigned char)*p;
    if (checksum != (unsigned char)(high * 16 + low)) return GPS_NMEA_INVALID;

    *asterisk = '\0';

    // Divide la frase in campi mantenendo anche quelli vuoti, per non spostare gli indici.
    char *fields[24];
    size_t count = 1;
    fields[0] = line;
    for (char *p = line; *p; ++p) {
        if (*p == ',') {
            if (count == sizeof(fields) / sizeof(fields[0])) return GPS_NMEA_INVALID;
            *p = '\0';
            fields[count++] = p + 1;
        }
    }

    // Riconosce le frasi RMC e GGA dal nome iniziale e ignora gli altri tipi.
    if (strlen(fields[0]) != 6 || fields[0][1] < 'A' || fields[0][1] > 'Z' ||
        fields[0][2] < 'A' || fields[0][2] > 'Z') return GPS_NMEA_NONE;
    const bool rmc = strcmp(fields[0] + 3, "RMC") == 0;
    const bool gga = strcmp(fields[0] + 3, "GGA") == 0;
    if (!rmc && !gga) return GPS_NMEA_NONE;

    // Prepara una lettura nuova e la salva soltanto se i campi superano i controlli.
    gps_reading_t next = {0};
    if (!(rmc ? parse_rmc(fields, count, &next) : parse_gga(fields, count, &next)))
        return GPS_NMEA_INVALID;
    *reading = next;
    return rmc ? GPS_NMEA_RMC : GPS_NMEA_GGA;
}

// Scarta la frase parziale e torna in attesa dell'inizio del prossimo messaggio.
void gps_nmea_reset(gps_nmea_parser_t *parser)
{
    parser->used = 0;
    parser->collecting = false;
}

// Riceve un byte alla volta e ricostruisce la frase anche se arriva in blocchi separati.
gps_nmea_result_t gps_nmea_feed(gps_nmea_parser_t *parser,
                                unsigned char byte, gps_reading_t *reading)
{
    // Quando arriva il simbolo $, inizia una nuova frase e abbandona quella eventualmente incompleta.
    if (byte == '$') {
        parser->used = 0;
        parser->collecting = true;
    }
    if (!parser->collecting) return GPS_NMEA_NONE;
    // Alla fine della riga chiude il testo e lo passa alla funzione che interpreta la frase.
    if (byte == '\n') {
        parser->line[parser->used] = '\0';
        gps_nmea_reset(parser);
        return parse_line(parser->line, reading);
    }

    // Ignora il ritorno a capo CR e scarta gli altri byte che non sono caratteri di testo.
    if (byte == '\r') return GPS_NMEA_NONE;
    if (byte < 32 || byte > 126) {
        gps_nmea_reset(parser);
        return GPS_NMEA_INVALID;
    }

    // Se manca spazio, scarta la frase prima di scrivere oltre il limite del buffer.
    if (parser->used >= sizeof(parser->line) - 1) {
        gps_nmea_reset(parser);
        return GPS_NMEA_OVERFLOW;
    }
    parser->line[parser->used++] = (char)byte;
    return GPS_NMEA_NONE;
}
