# AYE VISOR Firmware

[![Version](https://img.shields.io/badge/version-V45.5.12-blue)](https://github.com/AYE-Nautical/AYE_VISOR_FW/releases)
[![License](https://img.shields.io/badge/license-Proprietary-red)](LICENSE)
[![Hardware](https://img.shields.io/badge/hardware-ESP32--S3%20Eye-green)](https://www.waveshare.com)
[![Companion](https://img.shields.io/badge/companion-AYE%20POD-orange)](https://github.com/AYE-Nautical/AYE_POD_FW)

Firmware ufficiale per l'**AYE VISOR** — display di bordo a riflessione per la telemetria nautica in tempo reale, sviluppato da **AYE Nautical Systems**.

Il Visore riceve i dati dal [AYE POD](https://github.com/AYE-Nautical/AYE_POD_FW) via protocollo **ESP-NOW** e li mostra su display reflective 400×300 a 1-bit, ottimizzato per la visibilità in condizioni di piena luce solare.

---

## Architettura

```
┌─ AYE POD ───────────────────────────────────────────┐
│  ESP-NOW TX @ 10Hz — struct_nautica (87 bytes)      │
│  SOG · COG · HDG · AWA · AWS · TWA · TWS · TWD      │
│  VMG · ROLL · PITCH · GPS timestamp · anchor alert  │
└────────────────────────┬────────────────────────────┘
                         │ ESP-NOW (2.4 GHz)
┌────────────────────────▼────────────────────────────┐
│  AYE VISOR                                          │
│  loop() LVGL @ 200Hz                                │
│  scansionaCanaliRadio() — auto-hop CH 1-13          │
│  OnDataRecv() → aggiornaUI() → display refresh      │
│  SHTC3 I2C → temperatura / umidità locale           │
│  Batteria ADC PIN 4                                 │
│  Pulsante PIN 0 — navigazione schermate             │
└─────────────────────────────────────────────────────┘
```

## Hardware

| Componente | Modello | Interfaccia |
|---|---|---|
| MCU | ESP32-S3 (display integrato) | — |
| Display | Reflective LCD 400×300 1-bit | SPI |
| Sensore ambientale | SHTC3 | I2C SDA=13 SCL=14 |
| Batteria ADC | PIN 4 | ADC |
| Pulsante | PIN 0 | GPIO INPUT_PULLUP |
| UI Framework | LVGL v8 | — |

## Schermate

| Screen | Contenuto | Font |
|---|---|---|
| 0 — Main | SOG · HDG · AWA · AWS · TWS · TWD | stretto_80 |
| 1 — SOG + HDG | Valori grandi separati | stretto_192 + stretto_80 |
| 2 — TWS + AWA | Vento vero + angolo apparente | stretto_192 + stretto_80 |
| 3 — VMG + TWD | VMG + direzione vento vero | stretto_192 + stretto_80 |
| 4 — HEEL + TRIM | Sbandamento e assetto | stretto_192 |
| 5 — Sessione | Avvio / stop registrazione | montserrat |
| 6 — Summary | Riepilogo sessione conclusa | montserrat |
| 7 — Info | FW · PIN crew · QR App Mate | montserrat |

## File del pacchetto

| File | Funzione |
|---|---|
| `AYE_Visore_V38.ino` | Main — setup(), loop(), UI LVGL, ESP-NOW RX |
| `display_bsp.cpp` | Driver SPI display reflective RLCD |
| `display_bsp.h` | Header driver display |
| `src/app_bsp/lvgl_bsp.h` | Porting LVGL per ESP32-S3 |
| `src/app_bsp/lvgl_bsp.cpp` | Implementazione flush callback LVGL |
| `lv_font_stretto_80.c` | Font custom AYE 80px — cifre nautiche |
| `lv_font_stretto_160.c` | Font custom AYE 160px |
| `lv_font_stretto_192.c` | Font custom AYE 192px |
| `aye_logo.c` | Logo AYE — INDEXED_1BIT 400×222 |
| `QR_AyeNautical_Mate.c` | QR statico ayenautical.it/mate — ALPHA_1BIT |
| `secrets.h` | ⚠️ File locale — NON incluso nel repo (vedi sotto) |

## Configurazione — secrets.h

Prima di compilare, crea il file `secrets.h` nella cartella dello sketch copiando il template:

```bash
cp secrets.h.example secrets.h
```

Poi compila il file con i dati del tuo sistema. Il file non viene mai committato su GitHub (vedi `.gitignore`).

## Protocollo ESP-NOW

Il Visore cerca il POD in scan automatico sui canali 1-13.
La struttura dati ricevuta (`struct_nautica`, 87 bytes) è definita nel firmware POD ed è il contratto di interfaccia tra i due dispositivi.

**Compatibilità**: la `struct_nautica` deve essere identica byte-per-byte tra POD e Visore. Un mismatch causa ricezione silente di dati corrotti. Verificare sempre `sizeof(struct_nautica)` in fase di sviluppo.

## Versionamento

Schema **SemVer** (MAJOR.MINOR.PATCH), allineato al POD:

| Tipo | Quando |
|---|---|
| PATCH | Bugfix UI, tuning font, fix visualizzazione |
| MINOR | Nuove schermate o funzionalità retrocompatibili |
| MAJOR | Breaking change `struct_nautica` o cambio display |

## Dipendenze librerie Arduino

- **LVGL** v8.x
- **Adafruit SHTC3**
- **esp_now** (ESP-IDF, incluso in arduino-esp32)
- **esp_wifi** (ESP-IDF)

## Licenza

© 2026 AYE Nautical Systems. Tutti i diritti riservati.
Vedere [LICENSE](LICENSE) per i termini d'uso completi.

---

> ⚠️ Questo repository è **source available** ma non open source.
> Il codice è visibile a scopo di audit e trasparenza tecnica.
> I font custom (`lv_font_stretto_*.c`) e gli asset grafici sono opere originali
> di AYE Nautical Systems e non possono essere riutilizzati senza autorizzazione.
> Qualsiasi uso commerciale richiede accordo scritto preventivo.
