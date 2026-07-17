# Contributing — AYE VISOR Firmware

Questo repository è gestito internamente da **AYE Nautical Systems**.

## Segnalare un Bug

Apri una Issue su GitHub con:

- Versione firmware (es. `V45.5.12`)
- Versione firmware POD abbinato (es. `2.2.6`)
- Descrizione del comportamento osservato
- Schermata interessata (es. Screen 2 — TWS/AWA)
- Log seriale se disponibile (115200 baud)

## Compatibilità ESP-NOW

Se segnali un problema di comunicazione POD↔Visore, includi sempre:
- Output seriale di entrambi i dispositivi al boot (riga `struct_nautica=XX`)
- I due valori `sizeof(struct_nautica)` devono essere identici

## Pull Request Esterne

Le contribuzioni esterne **non sono accettate** senza accordo scritto preventivo con AYE Nautical Systems.

Per collaborazioni, licensing o integrazioni: **ayenautical@gmail.com**
