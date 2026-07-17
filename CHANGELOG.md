# Changelog — AYE VISOR Firmware

Tutte le modifiche rilevanti sono documentate in questo file.
Formato: [SemVer](https://semver.org/) — MAJOR.MINOR.PATCH

---

## [V45.6.1] — 2026-07-16
### Fix
- "POD offline dopo lo stop dal Visore": anti-scan esteso a `SCAN_GRACE_MS` (20s) oltre lo stop sessione. Prima `WiFi.scanNetworks()` cambiava canale radio mentre il POD era occupato nell'HTTPS di chiusura → POD spariva dalla web app 145-209s. Finestra one-shot; riaggancio dopo LINK LOST invariato.
- Invariati: struct 22/108 byte, protocollo a stato.

---

## [V45.6.0] — 2026-07-16
### Modificato (MINOR)
- Sessione da COMANDO a STATO: il Visore dichiara `sessione_attiva` (0/1) ripetuto @10Hz invece dell'evento `cmd_sessione` (1/2). Uno stato è idempotente: pacchetti persi non fanno divergere la sessione.
- ⚠ Flashare INSIEME al POD 2.6.0 (stesso campo, semantica diversa).
- Fix latente: stato + TX avvengono prima del lock LVGL (uno stop poteva non raggiungere mai il POD).
- Struct: `cmd_sessione` → `sessione_attiva` (int32_t); `struct_messaggio_visore` resta 22 byte, `struct_nautica` resta 108.
### Rimosso
- Latch `cmd_arm_time` + blocco sgancio 8s (non più necessario con lo stato).

---

## [V45.5.22] — 2026-07-16
### Fix
- Avvio sessione inviava `cmd=2` invece di `cmd=1`: rimossa la riga (introdotta in V45.5.21) che falsificava lo stato tasto in `execute_start()` → nessuna sessione veniva aperta a DB.

---

## [V45.5.20] — 2026-07-15
### Fix
- LINK LOST durante avvio sessione: con `session_active` niente `WiFi.scanNetworks()` bloccante; il Visore si riposiziona solo sul canale noto del POD.

---

## [V45.5.19] — 2026-07-15
### Fix
- Regressione V45.5.18: `pending_cloud_upload` bloccava `inviaDatiAlPod()` → POD scollegato in loop. Ora la TX ESP-NOW parte sempre con nuovi dati; il gate resta solo per il label UI.

---

## [V45.5.18] — 2026-07-15
### Fix
- Latch `cmd_sessione` fragile: timeout assoluto 8s (`cmd_arm_time`, ex `cmd_latch_time`) — prima il comando restava incollato e riapriva la sessione ogni 2s.
- Trigger DB `gestisci_sessione_automatica`: aggiunto `avviata_da` (era NULL invece di `'visore'`) — migration SQL separata.
- Documentati: cast `OnDataRecv` (ESP32 core 3.x) e Serial multi-core (mutex rinviato a V45.6.0).

---

## [V45.5.17] — 2026-07-15
### Fix
- Riaggancio via scan SSID `AYE_POD_NET` (max 1/3s) invece di hopping cieco 1..13: dà il canale esatto dell'AP del POD (che segue il router).

---

## [V45.5.16] — 2026-07-15
### Fix
- Hopping perpetuo dopo LINK LOST: se il canale del POD è noto il Visore resta parcheggiato lì (sweep completo solo se canale mai appreso o dopo 20s di grace).

---

## [V45.5.15] — 2026-07-15
### Fix
- Canale ESP-NOW TX: al primo pacchetto `canaleAttuale` allineato al canale RX reale (`esp_wifi_get_channel`) — prima `esp_now_send()` tornava OK senza consegnare.

---

## [V45.5.14] — 2026-07-15
### Fix
- Peer ESP-NOW: al primo pacchetto il peer viene ri-registrato col MAC reale del POD (non più il BSSID dell'AP); `esp_now_send()` ora controlla il valore di ritorno.

---

## [V45.5.13] — 2026-07-15
### Fix
- Filtro MAC sorgente in `OnDataRecv()`: pacchetti da MAC non abbinato scartati (`[RADIO-WARN]`).
- Schermata 4 (DTW): decimali da 2 a 1 cifra.

---

## [V45.5.12] — 2026-07-16
### Modificato
- Screen 0: SOG, AWS, TWS in `stretto_80` (rimossi override `montserrat_16`)
- Screen 1: SOG intero `stretto_192` + decimale `stretto_80`; HDG `stretto_192`
- Screen 2: TWS intero `stretto_192` + decimale `stretto_80`; AWA `stretto_192`
- Screen 3: VMG intero `stretto_192` + decimale `stretto_80`; TWD `stretto_192`
- Aggiunta `crea_meta_schermo_192_nodec()` — layout split intero+decimale

---

## [V45.5.11] — 2026-07-14
### Modificato
- QR: bitmap `ALPHA_1BIT` verificato funzionante
- Screen 0 AWA: rimosso `><`, intero assoluto + D/S `montserrat_32` a dx
- Screen 0 AWS: aggiunto 1 decimale in `montserrat_16`
- Screen 2 TWS: intero `stretto_80` + decimale separato `montserrat_32`
- Screen 3 VMG: intero `stretto_80` + decimale separato `montserrat_32`
- Aggiunta `crea_meta_schermo_80_nodec()` — layout split int+dec

---

## [V45.5.10] — 2026-07-12
### Modificato
- QR: salvato file `ALPHA_1BIT` verificato funzionante
- Screen 0 AWA: rimosso `><`, intero assoluto + D/S

---

## [V45.5.9] — 2026-07-11
### Aggiunto
- QR: bitmap da PNG 370×370 scalato 120×120 (`INDEXED_1BIT`, leggibile)
- Font `stretto_80` con decimale per SOG, TWS, VMG

---

## [V45.5.6] — 2026-07-09
### Aggiunto
- `struct_nautica` aggiornata a 87 bytes (allineata a POD V45.4.7)
- Nuovi campi: `uint32_t unix_timestamp`, `bool anchor_alert`
- Ora CET calcolata da `unix_timestamp` GPS senza NTP

---

## [V45.5.5] — 2026-07-07
### Aggiunto
- Screen 5: HEEL/TRIM con font `stretto_192`
- Paginazione: 0=main 1=SOG+HDG 2=TWS+AWA 3=VMG+TWD 4=HEEL+TRIM 5=session 6=summary 7=info

---

## [V45.5.0] — 2026-07-03
### Aggiunto
- Boot: logo AYE (`INDEXED_1BIT`) con barra di progresso
- Top bar: GPS | CLOUD | BAT VISORE
- Screen Info: temperatura, umidità, batteria POD e VISORE
- Font `stretto_192.c` — prima introduzione
- `aye_logo.c` — prima introduzione

---

## [V44.4.0] — 2026-06-20
### Aggiunto
- `struct_nautica` con `codice_crew[8]` e `fw_str[12]`
- Schermata INFO: mostra POD ID, PIN crew e FW POD ricevuti via ESP-NOW
- Prima versione pubblica del repository

---

> Le versioni precedenti a V44.4.0 sono disponibili nell'archivio interno.
