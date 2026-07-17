# Copyright Headers — AYE VISOR FW

Aggiungere questi header in cima a ciascun file **prima del push**.

---

## `AYE_Visore_V38.ino` — in cima al file, prima di ogni #include

```cpp
// =========================================================================
// AYE VISOR — Firmware principale
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
//
// Questo file è parte del firmware proprietario AYE VISOR.
// Source available — vietato l'uso commerciale senza autorizzazione scritta.
// Licenza: vedere LICENSE nel repository.
// Contatto: ayenautical@gmail.com
// =========================================================================
```

---

## `display_bsp.cpp` — in cima al file, prima di #include

```cpp
// =========================================================================
// AYE VISOR — Display BSP (Board Support Package)
// Driver SPI per display reflective RLCD 400×300 1-bit
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
//
// Questo file è parte del firmware proprietario AYE VISOR.
// Source available — vietato l'uso commerciale senza autorizzazione scritta.
// Licenza: vedere LICENSE nel repository.
// Contatto: ayenautical@gmail.com
// =========================================================================
```

---

## `display_bsp.h` — in cima al file, prima di #pragma once

```cpp
// =========================================================================
// AYE VISOR — Display BSP Header
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
//
// Source available — vietato l'uso commerciale senza autorizzazione scritta.
// Licenza: vedere LICENSE nel repository.
// =========================================================================
```

---

## `lv_font_stretto_80.c` / `lv_font_stretto_160.c` / `lv_font_stretto_192.c`
### Header identico per tutti e tre i file

```cpp
// =========================================================================
// AYE VISOR — Font custom "Stretto" [80|160|192]px
// Opera originale di AYE Nautical Systems.
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
//
// Questi glyph bitmap sono stati progettati specificamente per display
// reflective a 1-bit in condizioni di navigazione (piena luce solare).
// È vietato copiare, adattare o riutilizzare questi font in qualsiasi
// altro progetto, commerciale o non, senza autorizzazione scritta.
// Licenza: vedere LICENSE nel repository.
// Contatto: ayenautical@gmail.com
// =========================================================================
```

---

## `aye_logo.c`

```cpp
// =========================================================================
// AYE VISOR — Logo AYE Nautical Systems
// Asset grafico proprietario — INDEXED_1BIT 400×222
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
// =========================================================================
```

---

## `QR_AyeNautical_Mate.c`

```cpp
// =========================================================================
// AYE VISOR — QR Code statico ayenautical.it/mate
// ALPHA_1BIT 120×120 — generato per AYE App Mate
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
// =========================================================================
```
