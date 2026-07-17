#!/bin/bash
# =========================================================================
# AYE VISOR FW — apply_copyright_headers.sh
# Aggiunge il copyright header in cima a ciascun file sorgente.
# Eseguire UNA SOLA VOLTA prima del primo commit su GitHub.
# Uso: bash apply_copyright_headers.sh
# =========================================================================

YEAR=2026
COMPANY="AYE Nautical Systems"
CONTACT="ayenautical@gmail.com"
LICENSE_URL="vedere LICENSE nel repository"

apply_header() {
  local FILE="$1"
  local DESCRIPTION="$2"
  local TEMP=$(mktemp)

  # Controlla se l'header è già presente
  if grep -q "AYE Nautical Systems" "$FILE" 2>/dev/null; then
    echo "  [SKIP] $FILE — header già presente"
    return
  fi

  if [ ! -f "$FILE" ]; then
    echo "  [MISS] $FILE — file non trovato"
    return
  fi

  cat > "$TEMP" <<EOF
// =========================================================================
// AYE VISOR — $DESCRIPTION
// © $YEAR $COMPANY. Tutti i diritti riservati.
//
// Source available — vietato l'uso commerciale senza autorizzazione scritta.
// Licenza: $LICENSE_URL
// Contatto: $CONTACT
// =========================================================================
EOF
  cat "$FILE" >> "$TEMP"
  mv "$TEMP" "$FILE"
  echo "  [OK]   $FILE"
}

apply_header_font() {
  local FILE="$1"
  local SIZE="$2"
  local TEMP=$(mktemp)

  if grep -q "AYE Nautical Systems" "$FILE" 2>/dev/null; then
    echo "  [SKIP] $FILE — header già presente"
    return
  fi

  if [ ! -f "$FILE" ]; then
    echo "  [MISS] $FILE — file non trovato"
    return
  fi

  cat > "$TEMP" <<EOF
// =========================================================================
// AYE VISOR — Font custom "Stretto" ${SIZE}px
// Opera originale di $COMPANY.
// © $YEAR $COMPANY. Tutti i diritti riservati.
//
// Questi glyph bitmap sono stati progettati specificamente per display
// reflective a 1-bit in condizioni di navigazione (piena luce solare).
// E' vietato copiare, adattare o riutilizzare questi font in qualsiasi
// altro progetto, commerciale o non, senza autorizzazione scritta.
// Licenza: $LICENSE_URL
// Contatto: $CONTACT
// =========================================================================
EOF
  cat "$FILE" >> "$TEMP"
  mv "$TEMP" "$FILE"
  echo "  [OK]   $FILE"
}

echo "================================================"
echo "  AYE VISOR FW — Copyright Header Applicator"
echo "  $(date '+%Y-%m-%d %H:%M')"
echo "================================================"
echo ""
echo "Applicazione header ai file sorgente..."
echo ""

apply_header "AYE_Visore_V38.ino"    "Firmware principale"
apply_header "display_bsp.cpp"        "Display BSP — Driver SPI RLCD 400x300 1-bit"
apply_header "display_bsp.h"          "Display BSP Header"
apply_header "aye_logo.c"             "Logo AYE Nautical Systems — INDEXED_1BIT 400x222"
apply_header "QR_AyeNautical_Mate.c"  "QR Code statico ayenautical.it/mate — ALPHA_1BIT 120x120"

apply_header_font "lv_font_stretto_80.c"   "80"
apply_header_font "lv_font_stretto_160.c" "160"
apply_header_font "lv_font_stretto_192.c" "192"

echo ""
echo "================================================"
echo "  Completato."
echo "  Verifica con: head -5 <nomefile>.c"
echo "  Poi: git add . && git commit -m 'Add copyright headers'"
echo "================================================"
