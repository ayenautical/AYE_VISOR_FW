# Changelog — AYE VISOR Firmware

Tutte le modifiche rilevanti sono documentate in questo file.
Formato: [SemVer](https://semver.org/) — MAJOR.MINOR.PATCH

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
