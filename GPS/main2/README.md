
# GPS NEO-6M: lettura e conversione per ESP32-WROOM-32

Progetto autonomo in C con ESP-IDF 6.1.1. Riceve il GPS su UART2 a 9600 baud,
ricostruisce le frasi NMEA, verifica il checksum e mostra i dati convertiti
sulla console USB/UART0 a 115200 baud.

## Collegamenti

| NEO-6M | ESP32-WROOM-32 |
| --- | --- |
| TX | RX2 / GPIO16 |
| RX | TX2 / GPIO17, facoltativo per la sola ricezione |
| GND | GND |
| VCC | Secondo le specifiche del breakout effettivo |

I numeri indicano GPIO, non posizioni fisiche dei pin. I segnali UART sono a
3,3 V. Verificare l'alimentazione della schedina GPS: il modulo NEO-6M nudo
richiede 2,7-3,6 V, mentre alcuni breakout aggiungono un regolatore.

## Compilare e avviare questa versione

Aprire **questa cartella `GPS/main2`** in VS Code, oppure il suo file
`Neverina-GPS-ESP32.code-workspace`. In un terminale ESP-IDF:

```sh
idf.py build
idf.py -p COM5 flash monitor
```

Sostituire `COM5` con la porta della propria scheda. `flash` sostituisce il
firmware presente; Ctrl+] chiude il monitor. Il target e' `esp32` e la flash
configurata e' da 2 MB, da verificare sulla scheda effettiva.

Il progetto `GPS/platformio.ini` nella cartella superiore continua a puntare
a `GPS/main`, cioe' l'altra versione. Per compilare **main2** usare i comandi
qui sopra da **`GPS/main2`**.

```text
main2/
  CMakeLists.txt          progetto ESP-IDF autonomo
  main/
    CMakeLists.txt        componente dell'applicazione
    main.c                UART e riepiloghi sulla console
    gps_nmea.c            parser e conversioni in C standard
    gps_nmea.h            tipi dati e interfaccia del parser
  tests/                  prove del parser senza scheda
  samples/demo.nmea       sei frasi GPS sintetiche
```

Il precedente `main2/main.c` ora e' in **`main2/main/main.c`**: la sottocartella
`main` permette di compilare questa versione come progetto ESP-IDF completo.

## Conversioni e comportamento

- Coordinate NMEA `ddmm.mmmm` / `dddmm.mmmm` in gradi decimali:
  `gradi + minuti / 60`, con segno negativo per Sud e Ovest; tipo `double`.
- Velocita' al suolo da nodi a km/h: `nodi * 1.852`.
- Rotta al suolo in gradi rispetto al Nord vero, da RMC. Non e' una bussola.
- Quota sul livello medio del mare in metri, satelliti **usati**, qualita'
  del fix e HDOP, da GGA. HDOP e' adimensionale, non un errore in metri.
- Data e ora UTC da RMC; GGA contiene solo l'ora. I secondi frazionari sono
  accettati ma vengono mostrati come secondi interi. L'anno a due cifre usa
  esplicitamente la finestra 1980-2079. Nessuna conversione al fuso italiano.

Esempio dal campione (sono dati inventati, non una misura sulla barca):

```text
RMC | FIX OK | Lat: 45.650000 | Lon: 13.770000 | Vel: 15.19 km/h | Rotta: 87.40 deg | 12/11/2026 12:00:01 UTC
GGA | FIX OK | Lat: 45.650000 | Lon: 13.770000 | Alt: 2.4 m | Sat usati: 8 | HDOP: 0.90 | Qualita: 1 | 12:00:01 UTC
```

Ogni riepilogo contiene soltanto dati della propria frase: non mescola
posizione, quota e satelliti di istanti diversi. I campi facoltativi assenti
appaiono come `--`. Lo stato RMC `V`, il mode `N` oppure la qualita' GGA `0`
producono `NESSUN FIX`, senza riproporre vecchie coordinate. `FIX OK` indica
che la frase dichiara dati validi: il valore GGA distingue anche fix stimati
o differenziali. Cinque secondi senza byte producono un avviso separato.

Sono supportati i talker a due lettere, inclusi `GP` e `GN`. Le altre frasi
NMEA vengono ignorate; checksum errati, coordinate fuori intervallo e campi
obbligatori mancanti vengono scartati. Il parser gestisce blocchi UART
frammentati, piu' frasi per blocco e righe troppo lunghe, recuperando dal
successivo `$`. Non richiede librerie GPS, Wi-Fi, MQTT o allocazioni dinamiche.

## Provare le conversioni senza ESP32

Su Windows con GCC nel PATH:

```powershell
.\tests\run-tests.ps1
```

Oppure passare il percorso del compilatore a `-Compiler`. Lo script compila
con `-Wall -Wextra -Wpedantic -Werror`, esegue i test e converte il campione.
L'eseguibile generato permette anche di leggere un file NMEA:

```powershell
.\build\tests\gps_nmea_tests.exe --file .\samples\demo.nmea
```

Su Linux/macOS con un compilatore C:

```sh
mkdir -p build/tests
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Imain main/gps_nmea.c tests/test_gps_nmea.c -lm -o build/tests/gps_nmea_tests
./build/tests/gps_nmea_tests
./build/tests/gps_nmea_tests --file samples/demo.nmea
```

Verifica del 19 novembre 2026: **59 casi NMEA** superati e **6 frasi del
campione convertite, 0 scartate**. Le prove coprono conversioni note,
emisferi, limiti delle coordinate, data e ora, perdita del fix, campi vuoti,
checksum, righe spezzate, overflow e recupero del flusso.

Anche la compilazione completa di **questo progetto `GPS/main2`** e' riuscita
con ESP-IDF 6.1.1 e GCC Xtensa 15.2.0 per ESP32: firmware, bootloader e tabella
delle partizioni generati. L'applicazione occupa 158352 byte dei 1048576
disponibili nella partizione configurata.

## Wokwi e prova reale

`diagram.json` e `wokwi.toml` predispongono ESP32 e console. Il diagramma
non include un generatore GPS: senza una sorgente UART a 9600 baud su GPIO16
si vedra' l'avviso di assenza dati. La simulazione completa non e' stata
eseguita. Antenna, acquisizione satellitare e ricezione sulla scheda restano
da verificare con il NEO-6M reale.
