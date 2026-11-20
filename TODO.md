# TODO — samu_paccò :smile:

> Roadmap di gruppo. Un item = una task di taglia confrontabile, con owner, scadenza,
> criterio di "fatto" (DoD) e link al codice.

## Legenda

**Modulo:** `[ESP]` firmware ESP32 · `[RPI]` Raspberry Pi · `[SW]` codice host/analisi ·
`[HW]` elettronica/meccanica · `[DOC]` documentazione

**Stati:** In corso · Ideazione · Bloccato · Completato

**Formato item:**

```
- [ ] **Titolo** — owner · scadenza · [TAG][TAG]
  - DoD: <cosa deve funzionare perché sia "fatto">
  - Dipende da: <item bloccante>
  - Link: file / PR / report
```

Regole rapide:

- niente epiche senza sotto-checklist;
- la DoD è verificabile (una misura, non "sembra funzionare");
- un item passa in 'Completato' solo con data e autore;
- un item in attesa di hardware o decisioni va in 'Bloccato', non resta "in corso".

---

## In corso

- [ ] **Ricevitore UDP su Raspberry + trasmettitore su ESP** — @da-assegnare · scadenza: — · [RPI][ESP]
  - DoD: l'ESP invia a `router.py` un flusso continuo di pacchetti per 10 min senza perdite (rate target: _da concordare_); il Raspberry logga i pacchetti su file con timestamp.
  - Checklist:
    - [x] Trasmettitore ESP: Wi-Fi STA con retry di connessione ([UDP/main/main.c](UDP/main/main.c))
    - [x] Ricevitore Python in ascolto su `0.0.0.0:65432` ([UDP/router.py](UDP/router.py))
    - [ ] SSID/password/server IP fuori dal codice (ora hardcoded in `main.c`)
    - [ ] Parsing del payload reale al posto dell'echo (`router.py` rispedisce i byte ricevuti)
    - [ ] Test di perdita pacchetti e latenza (N inviati vs N ricevuti)
  - Link: [UDP/](UDP), [UDP/platformio.ini](UDP/platformio.ini)

## Ideazione

Nota: cose a cui dover pensare e iniziare a fare.

- [ ] **Codice main definitivo — integrazione di tutti i sensori** — @da-assegnare · scadenza: — · [ESP]
  - DoD: un unico firmware avvia IMU, US e GPS in task separate e trasmette i campioni via UDP senza blocchi né race condition.
  - Dipende da: ricevitore UDP stabile, buffer per il salvataggio
  - Checklist:
    - [ ] Definire il formato campione comune (timestamp monotono + id sensore + payload)
    - [ ] Schedulazione e priorità delle task (IMU / US / GPS / UDP)
    - [ ] Degradazione controllata se un sensore non risponde
  - Link: [IMU/](IMU), [US/](US), [GPS/](GPS), [UDP/](UDP)

- [ ] **Buffer per il salvataggio dati** — @da-assegnare · scadenza: — · [ESP][SW]
  - DoD: i campioni sopravvivono a una disconnessione Wi-Fi di durata target _da concordare_ e vengono inviati tutti, in ordine, al ripristino del collegamento.
  - Dipende da: formato campione definito (item sopra)
  - Checklist:
    - [ ] Scelta del supporto: RAM, flash/SPIFFS o SD
    - [ ] Politica di overflow (scarta i più vecchi vs blocca la produzione)
    - [ ] Test: disconnessione di durata target → 0 campioni persi
  - Link: —

## Bloccati

- _(nessuno)_

## Completati

Nota: elementi completati e testati su ESP32 (dove non indicato diversamente).

- [x] **Codice IMU** — 2026-11-10 · @Neetre · [ESP]
  - BNO055 via I2C, lettura e log nel main loop.
  - Link: [IMU/src/main/main.c](IMU/src/main/main.c), [IMU/src/main/bno055.c](IMU/src/main/bno055.c)

- [x] **Codice US** — 2026-11-18 · @PeakToolsLab · [ESP]
  - Acquisizione con periferica RMT; risultati di test registrati.
  - Link: [US/src/us_acquisition.c](US/src/us_acquisition.c), [US/Risultati_test.txt](US/Risultati_test.txt)

- [x] **Codice GPS** — 2026-11-18 · @margheritaagiordani · [ESP]
  - NMEA su UART2 con validazione del checksum; variante minimale in `main2`.
  - Link: [GPS/main/main.c](GPS/main/main.c), [GPS/main2/main.c](GPS/main2/main.c)

- [x] **Report di caratterizzazione AJ-SR04M** — 2026-11-20 · @iricci · [DOC][SW]
  - Caratterizzazione del sensore a corto raggio: analisi, dati grezzi e PDF.
  - Link: [reports/ajsr04m-characterization/](reports/ajsr04m-characterization)
