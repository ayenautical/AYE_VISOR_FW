// =========================================================================
// AYE VISOR — Firmware principale
// © 2026 AYE Nautical Systems. Tutti i diritti riservati.
//
// Source available — vietato l'uso commerciale senza autorizzazione scritta.
// Licenza: vedere LICENSE nel repository
// Contatto: ayenautical@gmail.com
// =========================================================================
// =========================================================================
// AYE VISORE — AYE_Visore_V45_5.ino
//
// Release: V45.6.1
//
// Changelog V45.6.1 — FIX "POD offline dopo lo stop dal Visore" (PATCH):
//   SINTOMO (campo, 16/07): chiudendo la sessione dal Visore il POD spariva
//   dalla web app per 145-209s; si riprendeva solo staccando/riattaccando
//   il WiFi. Chiudendo dalla web app, invece, nessun problema (3.0s).
//
//   NON era il POD: nei dati il ciclo telemetria e' regolarissimo a 2.8s
//   PRIMA dello stop, poi un buco netto, poi di nuovo 2.9s. Un POD in crisi
//   avrebbe cicli irregolari; qui cade la RADIO.
//
//   CATENA:
//    1. stop_session() -> session_active=false
//    2. il POD nello stesso ciclo fa POST telemetria + RPC chiudi (2 TLS)
//       -> occupato ~3s, non trasmette ESP-NOW
//    3. il Visore non riceve per >2s -> cade la guardia "link vivo"
//    4. ma session_active e' gia' false -> la protezione V45.5.20 non vale piu'
//    5. WiFi.scanNetworks() parte: blocca la radio ~1.5s e CAMBIA CANALE
//       (POD e Visore sono WIFI_AP_STA: una sola radio, canale condiviso)
//    6. il POD torna a trasmettere ma il Visore e' altrove -> rescan ogni 3s
//       -> loop. Il workaround "stacca/riattacca WiFi" funzionava perche'
//       forzava il POD a ristabilire il canale.
//
//   FIX: la protezione anti-scan non finisce con la sessione ma dura
//   SCAN_GRACE_MS (20s) oltre lo stop — il tempo che il POD chiuda la
//   sessione a DB e torni a trasmettere (misurato: ~3s, 20s con margine).
//   In quella finestra il Visore non scansiona: si riposiziona solo sul
//   canale noto (non bloccante). Finestra one-shot: scaduta, il
//   comportamento torna normale e il riaggancio dopo LINK LOST resta intatto.
//
//   INVARIATI: struct 22/108 byte, protocollo a stato, latch assente.
//
// Release: V45.6.0
//
// Changelog V45.6.0 — Sessione: da COMANDO a STATO (MINOR):
//   Il Visore non manda piu' un EVENTO da far consumare al POD
//   (cmd_sessione=1 "avvia ora" / =2 "ferma ora") ma dichiara uno STATO
//   (sessione_attiva=0/1) che ripete su ogni pacchetto @10Hz.
//   Un evento va consumato esattamente una volta: se si perde, lo stato
//   diverge; se si consuma due volte, apre due sessioni. Un livello no:
//   se si perdono 50 pacchetti, il 51esimo rimette tutto a posto.
//
//   ⚠ FLASHARE INSIEME AL POD 2.6.0 — mismatch semantico:
//     il campo ha la stessa dimensione ma significato diverso. Visore
//     vecchio + POD nuovo: cmd=2 verrebbe letto come sessione_attiva=2
//     (!= 0) → sessione che non si chiude mai.
//
//   RIMOSSI:
//     - latch cmd_arm_time + blocco di sgancio a 8s (V45.5.18→22).
//       Non c'e' piu' nessun fronte da tenere armato.
//   MODIFICATI:
//     - execute_start()  → sessione_attiva = 1
//     - stop_session()   → sessione_attiva = 0
//     - pending_cloud_upload: la conferma non si basa piu' su "cmd tornato
//       a 0" (con un livello quello non e' piu' un evento) ma su un
//       pacchetto fresco ricevuto dal POD dopo lo stop.
//   FIX (latente, trovato in review):
//     - stop_session() settava il comando DOPO "if(!Lvgl_lock(-1))return;":
//       se il lock LVGL falliva, la sessione veniva chiusa localmente ma
//       lo stop non raggiungeva MAI il POD → sessione aperta a DB per
//       sempre. Ora stato + TX avvengono prima di toccare la UI.
//
//   STRUCT: cmd_sessione (int) → sessione_attiva (int32_t). Cambia solo
//     nome e semantica: sizeof(struct_messaggio_visore) resta 22 byte
//     (con 'bool' sarebbe 19 → MAJOR). struct_nautica resta 108.
//
// Release: V45.5.22
//
// Changelog V45.5.22 — FIX: avvio sessione inviava cmd=2 invece di cmd=1 (PATCH):
//   BUG: execute_start() conteneva la riga
//     btn_press_time=millis(); btn_state=false;
//   introdotta dalla patch V45.5.21 (proposta su un'ipotesi errata e mai
//   validata). Falsificare lo stato del tasto DENTRO execute_start() rende
//   incoerente il calcolo di 'dur' in read_button(): al rilascio successivo
//   dur = millis()-btn_press_time viene misurato da un istante che non
//   corrisponde a nessuna pressione reale. Con session_active=true e
//   current_page==6 la condizione dur>=3000 faceva scattare stop_session()
//   → il Visore trasmetteva cmd=2 al posto di cmd=1.
//   Conseguenza a valle: il trigger DB gestisci_sessione_automatica non
//   riceveva MAI un punto con cmd=1 → nessuna sessione veniva aperta.
//   EVIDENZA DB 16/07: 56 punti con cmd=2 in 3 minuti, ZERO punti cmd=1
//   (il POD postava regolarmente ogni ~2.8s: non era bloccato).
//   FIX: riga rimossa. read_button() gestisce btn_state e btn_press_time
//   da solo, come in V45.5.20 e in tutte le versioni precedenti funzionanti.
//   INVARIATI: latch cmd 8s (V45.5.18), inviaDatiAlPod sempre (V45.5.19),
//   no-scan durante sessione (V45.5.20), OnDataRecv senza deref (V45.5.18).
//
// Release: V45.5.20
//
// Changelog V45.5.20 — FIX: LINK LOST durante avvio sessione (PATCH):
//   ROOT CAUSE: scansionaCanaliRadio() chiamava WiFi.scanNetworks() (bloccante
//   ~1.5s) quando ultimoPacchetto diventava 0 durante la chiamata HTTPS del POD
//   (crea_sessione_da_visore, ~3-5s). Questo interrompeva ESP-NOW e innescava
//   un loop scan→no link→scan perpetuo.
//   Con V45.5.17 il latch da 4s + check POD online faceva scadere cmd prima
//   del LINK LOST a 5s. Con V45.5.18 il latch assoluto da 8s manteneva cmd=1
//   per tutta la durata dell'HTTPS, portando al LINK LOST e al loop scan.
//   FIX: durante sessione_active, scansionaCanaliRadio() NON esegue scan
//   bloccante — si riposiziona solo sul canalePodNoto e aspetta.
//
// Release: V45.5.19
//
// Changelog V45.5.19 — FIX REGRESSIONE: POD si scollegava durante sessione (PATCH):
//   BUG V45.5.18: pending_cloud_upload=true bloccava inviaDatiAlPod() nel loop.
//   Quando execute_start() settava pending_cloud_upload=true, la TX ESP-NOW
//   si interrompeva → il POD non riceveva più pacchetti → smetteva di trasmettere
//   → LINK LOST dopo 5s → ultimoPacchetto=0 → cmd_sessione incollato in loop.
//   FIX: inviaDatiAlPod() viene chiamato SEMPRE quando ci sono nuovi dati,
//   indipendente da pending_cloud_upload. Il gate pending_cloud_upload rimane
//   solo per l'aggiornamento del label UI (label_upload_status).
//
// Release: V45.5.18
//
// Changelog V45.5.18 — Fix latch cmd_sessione + hardening ESP-NOW (PATCH):
//
//   Task 1 [BLOCCANTE] — FW_VERSION bump:
//     #define FW_VERSION aggiornato a "V45.5.18" per distinguere le build.
//     Il binario V45.5.17 col fix era indistinguibile dal precedente.
//
//   Task 2 [ALTA] — Latch cmd_sessione fragile risolto:
//     BUG: se il POD spariva con cmd armato, cmd_latch_time veniva riarmato
//     a ogni ciclo loop() → il comando rimaneva incollato per sempre.
//     Sintomo osservato 15/07 14:40-14:45: cmd=1 per 103 punti / 5 minuti.
//     Ogni chiusura sessione era seguita da riapertura automatica entro 2s.
//     FIX: timeout assoluto 8s dall'armamento (variabile cmd_arm_time, settata
//     in execute_start() e stop_session()). Il POD cattura cmd con edge-trigger
//     al primo pacchetto utile — 8s = ~80 TX a 10Hz, sempre sufficienti.
//     Variabile rinominata: cmd_latch_time → cmd_arm_time.
//
//   Task 3 [MEDIA] — avviata_da corretto nel trigger DB:
//     Il trigger gestisci_sessione_automatica mancava del campo avviata_da
//     nell'INSERT → le sessioni risultavano avviata_da=NULL invece di 'visore'.
//     Fix: migration SQL separata (AYE_Visore_V45.5.18_DB_MIGRATION.sql).
//
//   Task 4 [BASSA] — Cast ESP-NOW latente documentato:
//     esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv): cast forzato
//     latente su ESP32 Arduino core 3.x (firma callback cambiata).
//     Sicuro oggi perché OnDataRecv() NON dereferenzia il parametro 'mac'
//     (bug root-cause del 12/07 risolto in V45.5.18). Commento protettivo
//     aggiunto. Fix definitivo firma pianificato in V45.6.0.
//
//   Task 5 [BASSA] — Serial multi-core (nota documentata):
//     Due core scrivono su Serial senza mutex → righe troncate nel log.
//     Nota documentata. Implementazione mutex rinviata a V45.6.0 per non
//     alterare il flusso di boot/diagnostica validato.
//
//   INVARIANTI:
//     struct_nautica = 108 bytes     — NON MODIFICATA
//     struct_messaggio_visore = 22 bytes — NON MODIFICATA
//     Paginazione 0..8               — INVARIATA
//     Fix V45.5.14 peer re-add       — PRESERVATO
//     Fix V45.5.17 scan SSID         — PRESERVATO
//     Fix cast OnDataRecv (V45.5.18) — PRESERVATO

// Changelog V45.5.17 — Riaggancio via scan SSID (PATCH):
//   - scansionaCanaliRadio(): sostituito l'hopping cieco 1..13 con una
//     ri-scansione dell'SSID AYE_POD_NET (max 1 ogni 3s). WiFi.scanNetworks()
//     da' il canale ESATTO dell'AP del POD, che cambia nel tempo perche' il
//     POD e' WIFI_AP_STA e segue il canale del router (ch=5 -> ch=2).
//   - OnDataRecv(): rimossa la lettura canale (inaffidabile nella callback).

// Changelog V45.5.16 — FIX hopping perpetuo dopo LINK LOST (PATCH):
//   - scansionaCanaliRadio(): se il canale del POD e' noto, il Visore resta
//     parcheggiato li' invece di scandire 1..13. Sweep completo solo se il
//     canale non e' mai stato appreso o dopo 20s di grace senza riaggancio.
//   - OnDataRecv(): memorizza canalePodNoto e azzera tempoLinkPerso.
//   Causa: il blocco LINK LOST azzera ultimoPacchetto -> la guardia
//   'ultimoPacchetto!=0' falliva sempre -> hopping infinito ogni 600ms.

// Changelog V45.5.15 — FIX canale ESP-NOW TX (PATCH):
//   - OnDataRecv(): al primo pacchetto, canaleAttuale viene allineato al
//     canale RX reale (esp_wifi_get_channel) e canaleTrovato forzato a true.
//     Senza, l'hopping poteva lasciare l'interfaccia su un canale diverso
//     da quello del POD: esp_now_send() tornava ESP_OK senza consegnare.
//   Nessun altro cambiamento.

// Changelog V45.5.14 — FIX peer ESP-NOW: TX verso POD non funzionante (PATCH):
//   - OnDataRecv(): al primo pacchetto ricevuto, il peer viene RI-REGISTRATO
//     con il MAC ESP-NOW reale del POD (prima restava quello del BSSID AP).
//   - inviaDatiAlPod(): esp_now_send() ora controlla il valore di ritorno.
//   Nessun altro cambiamento: UI, struct e paginazione invariate.

// Changelog V45.5.13 — Fix decimali DTW + Opzione C filtro MAC (PATCH):
//   - OnDataRecv(): aggiunto filtro MAC sorgente (memcmp con macPod[]).
//     Pacchetti da MAC non abbinato vengono scartati con log [RADIO-WARN].
//   - aggiornaUI() schermata 4 (DTW|BTW): DTW da 2 decimali a 1 decimale.
//     dtw_dec: * 100 → * 10. Formato: ".XX" → ".X". arrive_alert: ".00" → ".0".
// Changelog V45.5.12:
//   - struct_nautica: aggiornata a 108 bytes (allineata a POD V2.3.0)
//     Aggiunti in fondo (offset 87+): btw, dtw, wp_bearing_rel,
//     vmg_wp, eta_wp_sec, wp_arrive_alert.
//     Il campo 'vmg' (offset 54) rimane INVARIATO — nessun rinomino,
//     nessun impatto su aggiornaUI() esistente.
//   - Schermata 4 (NUOVA): DTW | BTW — navigazione waypoint
//     Inserita tra screen_giant_wp (pag 3) e screen_heel_trim (ora pag 5).
//     Bot bar: ETA mm:ss + VMG verso WP. Top bar cloud: "ARRIVED!" se alert.
//     Pannello DTW: decimali 2 cifre + label WP_REL (±° D/S) in basso.
//   - Paginazione: 0..8 (era 0..7). toggle_page: >8→0.
//   - Array barre (gl/cl/bl/ol) e link_lost: 8→9 elementi.
//   - aggiornaUI(): aggiunto blocco schermata 4 WP.
//   - loop() lastOraUpdate: ol[] 8→9 elementi + lbl_ora_wpn separato.
//
// Changelog V45.5.11 (precedente — codice base):
//   - Screen0: SOG, AWS, TWS in stretto_80 (rimossi override montserrat_16)
//   - Screen1: SOG intero stretto_192 + decimale stretto_80; HDG stretto_192
//   - Screen2: TWS intero stretto_192 + decimale stretto_80; AWA stretto_192
//   - Screen3: VMG intero stretto_192 + decimale stretto_80; TWD stretto_192
//   - crea_meta_schermo_192_nodec(): stretto_192 intero + stretto_80 decimale
// [Tutti i changelog V45.5.x precedenti invariati — vedere file sorgente]
// =========================================================================
#include "display_bsp.h"
#include "src/app_bsp/lvgl_bsp.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Adafruit_SHTC3.h>
#include <time.h>

// ── V45.5.12: aggiornato da V45.5.11 ─────────────────────────────────────
#define FW_VERSION "V45.6.1"
#define VISORE_BAT_PIN 4
#define VISORE_BTN_PIN 0
#define I2C_SDA 13
#define I2C_SCL 14

// ── Prototipi (invariati da V45.5.11) ────────────────────────────────────
static void Lvgl_FlushCallback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
void clear_screen_hw(uint8_t color);
void esegui_anti_ghosting();
int leggiBatteriaStabilizzata();
void aggiorna_boot_status(const char* testo, int percentuale);
void mostra_boot_logo();
void setupSensoriLocali();
void setupDisplayHardware();
void trovaPodSegugio();
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);
void setupRadioESPNOW();
void inviaDatiAlPod();
void applica_sfondo_blindato(lv_obj_t * parent_screen);
void crea_top_bar(lv_obj_t * p, lv_obj_t ** l_gps, lv_obj_t ** l_cloud, lv_obj_t ** l_bat);
void crea_bot_bar(lv_obj_t * p, lv_obj_t ** l_ora);
void crea_quadrante(lv_obj_t * parent, int x, int y, int w, int h, const char* titolo, const char* unita, lv_obj_t ** lbl_val);
void crea_quadrante_80(lv_obj_t * parent, int x, int y, int w, int h, const char* titolo, const char* unita, lv_obj_t ** lbl_val);
void crea_meta_schermo_80(lv_obj_t * parent, int x, const char* titolo, const char* unita, lv_obj_t ** lbl_val);
void crea_meta_schermo_80_nodec(lv_obj_t * parent, int x, const char* titolo, const char* unita, lv_obj_t ** lbl_val, lv_obj_t ** lbl_dec);
void crea_meta_schermo_192_nodec(lv_obj_t * parent, int x, const char* titolo, const char* unita, lv_obj_t ** lbl_val, lv_obj_t ** lbl_dec);
void crea_meta_schermo_160(lv_obj_t * parent, int x, const char* titolo, const char* unita, lv_obj_t ** lbl_val);
void crea_meta_schermo_192dec(lv_obj_t * parent, int x, const char* titolo, const char* unita, lv_obj_t ** lbl_val);
void crea_meta_schermo_192(lv_obj_t * parent, int x, const char* titolo, const char* unita, lv_obj_t ** lbl_val);
void costruisci_interfaccia();
void aggiornaDatiSchermataInfo();
void toggle_page();
void start_countdown();
void handle_countdown();
void execute_start();
void stop_session();
void read_button();
void aggiornaUI();
void scansionaCanaliRadio();
String getOraCET();

const char* target_ssid = "AYE_POD_NET";

Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();
bool shtc3_found = false;

DisplayPort RlcdPort(12, 11, 5, 40, 41, 400, 300);

// ── ENGINE QR (invariato da V45.5.11) ─────────────────────────────────────
namespace RealQr {
  const int QR_GRID_SIZE = 41;
  uint8_t moduli[QR_GRID_SIZE][QR_GRID_SIZE];
  bool is_function[QR_GRID_SIZE][QR_GRID_SIZE];
  uint8_t exp_t[256], log_t[256], gen_poly[35];

  void init_qr_tables() {
    int val=1;
    for(int i=0;i<255;i++){exp_t[i]=val;log_t[val]=i;val<<=1;if(val&0x100)val^=0x11D;}
    exp_t[255]=exp_t[0];
    memset(gen_poly,0,sizeof(gen_poly));gen_poly[0]=1;
    for(int i=0;i<34;i++){
      for(int j=i+1;j>0;j--){uint8_t p1=gen_poly[j]?exp_t[(log_t[gen_poly[j]]+i)%255]:0;gen_poly[j]=p1^gen_poly[j-1];}
      gen_poly[0]=gen_poly[0]?exp_t[(log_t[gen_poly[0]]+i)%255]:0;
    }
  }

  void applicaPatternFissi() {
    memset(moduli,0,sizeof(moduli));memset(is_function,0,sizeof(is_function));
    int angoli[3][2]={{0,0},{QR_GRID_SIZE-7,0},{0,QR_GRID_SIZE-7}};
    for(int a=0;a<3;a++){int ox=angoli[a][0],oy=angoli[a][1];
      for(int y=0;y<7;y++)for(int x=0;x<7;x++){is_function[oy+y][ox+x]=true;if(y==0||y==6||x==0||x==6||(y>=2&&y<=4&&x>=2&&x<=4))moduli[oy+y][ox+x]=1;}}
    for(int i=0;i<8;i++){is_function[7][i]=is_function[i][7]=is_function[QR_GRID_SIZE-8][i]=is_function[QR_GRID_SIZE-1-i][7]=is_function[7][QR_GRID_SIZE-1-i]=is_function[i][QR_GRID_SIZE-8]=true;}
    for(int i=8;i<QR_GRID_SIZE-8;i++){if(i%2==0){moduli[6][i]=1;moduli[i][6]=1;}}
    int ax=34,ay=34;for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){is_function[ay+y][ax+x]=true;if(y==-2||y==2||x==-2||x==2||(y==0&&x==0))moduli[ay+y][ax+x]=1;}
    moduli[33][8]=1;is_function[33][8]=true;
    for(int i=0;i<QR_GRID_SIZE;i++){is_function[8][i]=true;is_function[i][8]=true;}
  }

  void generaMatriceReale(const char* s, int len) {
    applicaPatternFissi();
    uint8_t qr_data[134];memset(qr_data,0,134);int bp=0;
    auto wb=[&](uint32_t v,int lb){for(int i=lb-1;i>=0;i--){if((v>>i)&1)qr_data[bp/8]|=(1<<(7-(bp%8)));bp++;}};
    wb(0x04,4);wb(len,8);for(int i=0;i<len;i++)wb(s[i],8);wb(0,4);
    if(bp%8)bp+=(8-(bp%8));bool tg=true;while(bp<134*8){wb(tg?0xEC:0x11,8);tg=!tg;}
    uint8_t ecc[34];memset(ecc,0,34);
    for(int i=0;i<134;i++){uint8_t f=qr_data[i]^ecc[0];memmove(ecc,ecc+1,33);ecc[33]=0;if(f)for(int j=0;j<34;j++)if(gen_poly[j])ecc[j]^=exp_t[(log_t[f]+log_t[gen_poly[j]])%255];}
    int db=0,col=40;bool mu=true;
    while(col>0){if(col==6)col--;
      for(int ri=0;ri<QR_GRID_SIZE;ri++){int r=mu?(40-ri):ri;for(int co=0;co<2;co++){int c=col-co;if(!is_function[r][c]){int bit=0;if(db<168*8){int by=db/8,bo=7-(db%8);bit=(by<134)?((qr_data[by]>>bo)&1):((ecc[by-134]>>bo)&1);db++;}if((r+c)%2==0)bit^=1;moduli[r][c]=bit;}}}col-=2;mu=!mu;}
    uint16_t fb=0x77C4;
    moduli[0][8]=(fb>>14)&1;moduli[1][8]=(fb>>13)&1;moduli[2][8]=(fb>>12)&1;moduli[3][8]=(fb>>11)&1;moduli[4][8]=(fb>>10)&1;moduli[5][8]=(fb>>9)&1;moduli[7][8]=(fb>>8)&1;moduli[8][8]=(fb>>7)&1;moduli[8][7]=(fb>>6)&1;moduli[8][5]=(fb>>5)&1;moduli[8][4]=(fb>>4)&1;moduli[8][3]=(fb>>3)&1;moduli[8][2]=(fb>>2)&1;moduli[8][1]=(fb>>1)&1;moduli[8][0]=(fb>>0)&1;
    moduli[8][40]=(fb>>14)&1;moduli[8][39]=(fb>>13)&1;moduli[8][38]=(fb>>12)&1;moduli[8][37]=(fb>>11)&1;moduli[8][36]=(fb>>10)&1;moduli[8][35]=(fb>>9)&1;moduli[8][34]=(fb>>8)&1;moduli[8][33]=(fb>>7)&1;
    moduli[34][8]=(fb>>6)&1;moduli[35][8]=(fb>>5)&1;moduli[36][8]=(fb>>4)&1;moduli[37][8]=(fb>>3)&1;moduli[38][8]=(fb>>2)&1;moduli[39][8]=(fb>>1)&1;moduli[40][8]=(fb>>0)&1;
  }
}

// ── STRUTTURE RADIO ────────────────────────────────────────────────────────
// CRITICO: struct_nautica IDENTICA al file AYE_POD V2.3.0 (108 bytes)
// Partenza da V45.5.11 (87 bytes) + aggiunta 6 campi WP in fondo (21 bytes)
// Il campo 'vmg' (offset 54) rimane INVARIATO — aggiornaUI usa vmg_wind, ok.
typedef struct __attribute__((packed)) struct_nautica {
  int roll, pitch, heading;
  float sog, awa, aws, twa, tws, twd;
  int batteria_pod;
  bool cloud_connesso, gps_fix;
  float lat, lon, cog, vmg, vmg_wind;  // 'vmg' invariato — offset 54
  char     codice_crew[8];
  char     fw_str[12];
  uint32_t unix_timestamp;
  bool     anchor_alert;               // offset 86 — invariato
  // ── NUOVI V45.5.12 — in fondo, stesso layout POD V2.3.0 ─────────────
  float    btw;             // offset 87  — bearing to waypoint (0-359°)
  float    dtw;             // offset 91  — distance to waypoint (nm)
  float    wp_bearing_rel;  // offset 95  — BTW relativo prora (±180°) D=pos S=neg
  float    vmg_wp;          // offset 99  — VMG verso waypoint (kn)
  uint32_t eta_wp_sec;      // offset 103 — ETA in secondi (0=N/A)
  bool     wp_arrive_alert; // offset 107 — arrive alarm (<50m)
} struct_nautica;
// sizeof = 108 bytes — verificato con simulazione g++

struct_nautica datiNautici;

// ── V45.6.0: da COMANDO a STATO ───────────────────────────────────────────
// cmd_sessione (evento: 1="avvia ora", 2="ferma ora") → sessione_attiva
// (stato: 0=non attiva, 1=attiva). Ogni pacchetto porta la verita' completa:
// se se ne perdono 50, il 51esimo rimette tutto a posto. Niente piu' latch.
//
// ⚠ IL CAMPO DEVE RESTARE A 4 BYTE — verificato con g++:
//     int      cmd_sessione     → sizeof = 22  (baseline)
//     int32_t  sessione_attiva  → sizeof = 22  ✅ OK, MINOR
//     bool     sessione_attiva  → sizeof = 19  ❌ ROMPE, sarebbe MAJOR
typedef struct __attribute__((packed)) struct_messaggio_visore {
  float   batteriaVisore;   // 4
  char    fwVisore[10];     // 10
  float   session_dist;     // 4
  int32_t sessione_attiva;  // 4  ← era 'int cmd_sessione' (stesso layout)
} struct_messaggio_visore;
// sizeof = 22 bytes — INVARIATO

struct_messaggio_visore datiDaInviare;

uint8_t macPod[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
volatile bool nuoviDatiRicevuti = false;
uint8_t canaleAttuale = 1;
unsigned long tempoUltimoCambioCanale = 0;
bool canaleTrovato = false;
// V45.5.16: canale su cui il POD e' stato effettivamente sentito (0=ignoto)
uint8_t canalePodNoto = 0;
unsigned long tempoLinkPerso = 0;
unsigned long ultimoPacchetto = 0;

// Paginazione V45.5.12: 0=main 1=maxi 2=wind 3=wp 4=wp_nav 5=heel 6=session 7=summary 8=info
// (era max=7, ora max=8 — inserita pag 4 tra pag3 e pag4 precedente)
int current_page = 0;
bool session_active = false, countdown_active = false, pending_cloud_upload = false;
float session_distance = 0.0;
// V45.6.0: rimossa cmd_arm_time — il latch non serve piu' con la logica
// a stato (non c'e' nessun fronte da tenere armato).
unsigned long session_time_elapsed = 0, last_calc_time = 0;
// V45.6.1: istante dello stop sessione (0 = nessuno stop recente).
// Tiene attiva la protezione anti-scan mentre il POD chiude la sessione a DB.
unsigned long t_fine_sessione = 0;
float session_max_sog=0,session_sum_sog=0; unsigned long session_count_sog=0;
float session_max_aws=0,session_sum_aws=0; unsigned long session_count_aws=0;
float session_avg_sog=0,session_avg_aws=0;

String formattaAngoloVento(float a){
  float av=abs(a);return a>=0?(String(av,0)+"\xBA D >"):("< S "+String(av,0)+"\xBA");
}

// Ora CET dal timestamp GPS (invariata da V45.5.11)
String getOraCET(){
  if(datiNautici.unix_timestamp == 0) return "--:--";
  uint32_t ts = datiNautici.unix_timestamp;
  uint32_t secs_today = ts % 86400UL;
  uint32_t days = ts / 86400UL;
  uint8_t  month_approx = (uint8_t)((days % 365) / 30) + 1;
  uint32_t offset = (month_approx >= 4 && month_approx <= 10) ? 7200UL : 3600UL;
  uint32_t local_secs = secs_today + offset;
  if(local_secs >= 86400UL) local_secs -= 86400UL;
  uint8_t hh = local_secs / 3600;
  uint8_t mm = (local_secs % 3600) / 60;
  char buf[6]; snprintf(buf,6,"%02d:%02d",hh,mm);
  return String(buf);
}

// ETA formattato (NUOVA V45.5.12)
static String formattaETA(uint32_t sec) {
  if (sec == 0) return "--:--";
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  char buf[12];
  if (h > 0) snprintf(buf, sizeof(buf), "%dh%02dm", (int)h, (int)m);
  else        snprintf(buf, sizeof(buf), "%d:%02d",  (int)m, (int)s);
  return String(buf);
}

bool btn_state=false; unsigned long btn_press_time=0;
int countdown_val=0; unsigned long countdown_last_tick=0;

// ── Font e immagini (invariati) ───────────────────────────────────────────
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_stretto_80);
LV_FONT_DECLARE(lv_font_stretto_160);
LV_FONT_DECLARE(lv_font_stretto_96);
LV_FONT_DECLARE(lv_font_stretto_192);
LV_IMG_DECLARE(aye_logo_ridotta);
LV_IMG_DECLARE(QR_AyeNautical_Mate);

// ── Oggetti globali schermate ─────────────────────────────────────────────
lv_obj_t *screen_main, *screen_maxi, *screen_session, *screen_summary;
lv_obj_t *screen_giant_wind, *screen_giant_wp, *screen_heel_trim, *screen_info;
// ── NUOVO V45.5.12 ────────────────────────────────────────────────────────
lv_obj_t *screen_wp_nav;                              // pag 4: DTW | BTW
lv_obj_t *lbl_gps_wpn, *lbl_cloud_wpn, *lbl_bat_wpn, *lbl_ora_wpn;
lv_obj_t *val_wp_dtw, *val_wp_dtw_dec;               // DTW nm (stretto_192 + stretto_80)
lv_obj_t *val_wp_btw;                                 // BTW deg (stretto_192)
lv_obj_t *val_wp_rel;                                 // WP_REL ±° D/S (montserrat_16)

// Valori screen_main (6 quadranti) — invariati
lv_obj_t *val_hdg,*val_sog,*val_aws,*val_awa,*val_tws,*val_twd;

// Barre top/bot per ogni schermata — 9 schermate in V45.5.12
lv_obj_t *lbl_gps_m,  *lbl_cloud_m,  *lbl_bat_m,  *lbl_ora_m;
lv_obj_t *lbl_gps_maxi,*lbl_cloud_maxi,*lbl_bat_maxi,*lbl_ora_maxi;
lv_obj_t *lbl_gps_gw, *lbl_cloud_gw, *lbl_bat_gw, *lbl_ora_gw;
lv_obj_t *lbl_gps_gwp,*lbl_cloud_gwp,*lbl_bat_gwp,*lbl_ora_gwp;
lv_obj_t *lbl_gps_ht, *lbl_cloud_ht, *lbl_bat_ht, *lbl_ora_ht;
lv_obj_t *lbl_gps_s,  *lbl_cloud_s,  *lbl_bat_s,  *lbl_ora_s;
lv_obj_t *lbl_gps_sum,*lbl_cloud_sum,*lbl_bat_sum,*lbl_ora_sum;
lv_obj_t *lbl_gps_info,*lbl_cloud_info,*lbl_bat_info,*lbl_ora_info;

// Valori schermate grandi — invariati
lv_obj_t *val_maxi_sog, *val_maxi_hdg;
lv_obj_t *val_maxi_sog_dec;
lv_obj_t *val_g_tws,    *val_g_awa;
lv_obj_t *val_g_awa_dir;
lv_obj_t *val_g_tws_dec;
lv_obj_t *val_g_vmg,    *val_g_twd;
lv_obj_t *val_g_vmg_dec;
lv_obj_t *val_awa_dir_m;
lv_obj_t *val_heel,     *val_trim;

// Sessione — invariata
lv_obj_t *cont_session_data,*label_session_instr,*label_start_flash,*label_countdown;
lv_obj_t *val_s_time,*val_s_dist,*val_s_hdg,*val_s_awa,*val_s_aws,*val_s_roll;

// Summary — invariata
lv_obj_t *val_sum_time,*val_sum_dist,*val_sum_sog,*val_sum_aws,*label_upload_status;

// Info — invariata
lv_obj_t *txt_info_pod,*txt_info_capt,*txt_info_pin,*txt_fw_pod,*txt_info_bat,*txt_info_env,*qr_img_obj;

// Boot
lv_obj_t *boot_status_label, *boot_bar;
char found_pod_ssid[32] = "AYE_POD_NET";

// ── HARDWARE (invariato da V45.5.11) ──────────────────────────────────────
static void Lvgl_FlushCallback(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *color_map){
  uint16_t *buf=(uint16_t*)color_map;
  for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++){RlcdPort.RLCD_SetPixel(x,y,(*buf<0x7fff)?ColorBlack:ColorWhite);buf++;}
  if(lv_disp_flush_is_last(drv))RlcdPort.RLCD_Display();
  lv_disp_flush_ready(drv);
}
void clear_screen_hw(uint8_t c){for(int y=0;y<300;y++)for(int x=0;x<400;x++)RlcdPort.RLCD_SetPixel(x,y,c);RlcdPort.RLCD_Display();}
void esegui_anti_ghosting(){clear_screen_hw(ColorBlack);delay(100);clear_screen_hw(ColorWhite);delay(50);}
int leggiBatteriaStabilizzata(){static float sr=0;int r=analogRead(VISORE_BAT_PIN);if(sr<100)sr=r;else sr=sr*.95+r*.05;return constrain(map((int)sr,1300,1683,0,100),0,100);}
void aggiorna_boot_status(const char* t,int p){if(Lvgl_lock(-1)){if(boot_status_label)lv_label_set_text(boot_status_label,t);if(boot_bar)lv_bar_set_value(boot_bar,p,LV_ANIM_ON);Lvgl_unlock();}lv_timer_handler();}

void mostra_boot_logo(){
  lv_obj_set_style_bg_color(lv_scr_act(),lv_color_white(),0);
  lv_obj_set_style_bg_opa(lv_scr_act(),LV_OPA_COVER,0);
  lv_obj_t *logo_img = lv_img_create(lv_scr_act());
  lv_img_set_src(logo_img, &aye_logo_ridotta);
  lv_obj_align(logo_img, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_t *info_row = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(info_row);
  lv_obj_set_size(info_row, 400, 20);
  lv_obj_align(info_row, LV_ALIGN_TOP_LEFT, 0, 232);
  lv_obj_t *titolo = lv_label_create(info_row);
  lv_label_set_text(titolo, "AYE VISOR");
  lv_obj_set_style_text_font(titolo, &lv_font_montserrat_14, 0);
  lv_obj_align(titolo, LV_ALIGN_LEFT_MID, 5, 0);
  lv_obj_t *fw_lbl = lv_label_create(info_row);
  lv_label_set_text_fmt(fw_lbl, "FW %s", FW_VERSION);
  lv_obj_set_style_text_font(fw_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(fw_lbl, LV_ALIGN_CENTER, 0, 0);
  boot_status_label = lv_label_create(info_row);
  lv_label_set_text(boot_status_label, "AVVIO...");
  lv_obj_set_style_text_font(boot_status_label, &lv_font_montserrat_14, 0);
  lv_obj_align(boot_status_label, LV_ALIGN_RIGHT_MID, -5, 0);
  boot_bar = lv_bar_create(lv_scr_act());
  lv_obj_set_size(boot_bar, 360, 8);
  lv_obj_align(boot_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
}

void setupSensoriLocali(){Wire.begin(I2C_SDA,I2C_SCL);if(shtc3.begin())shtc3_found=true;}
void setupDisplayHardware(){RlcdPort.RLCD_Init();Lvgl_PortInit(400,300,Lvgl_FlushCallback);if(Lvgl_lock(-1)){mostra_boot_logo();Lvgl_unlock();};}

void trovaPodSegugio(){
  WiFi.mode(WIFI_STA);WiFi.disconnect();int ch=-1,tent=0;
  while(ch<0){tent++;aggiorna_boot_status(("Ricerca POD ("+String(tent)+")...").c_str(),10+(tent*5)%80);
    int n=WiFi.scanNetworks();for(int i=0;i<n;i++){if(WiFi.SSID(i)==target_ssid){ch=WiFi.channel(i);memcpy(macPod,WiFi.BSSID(i),6);
      strncpy(found_pod_ssid,target_ssid,32);
      aggiorna_boot_status("POD COLLEGATO!",100);
      esp_wifi_set_promiscuous(true);esp_wifi_set_channel(ch,WIFI_SECOND_CHAN_NONE);esp_wifi_set_promiscuous(false);break;}}
    if(ch<0)delay(1000);WiFi.scanDelete();}
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  // ── V45.5.18 — FIX CRITICO: rimosso l'apprendimento del MAC ────────────
  // BUG (tutte le versioni precedenti): esp_now_register_recv_cb() veniva
  // chiamata con un cast forzato (esp_now_recv_cb_t). Su ESP32 Arduino
  // core 3.x la firma della callback e' CAMBIATA:
  //   core 2.x: (const uint8_t *mac_addr, const uint8_t *data, int len)
  //   core 3.x: (const esp_now_recv_info_t *info, const uint8_t *data, int len)
  // Il cast zittisce il compilatore, ma a runtime 'mac' NON e' un MAC:
  // e' un puntatore a esp_now_recv_info_t{ uint8_t *src_addr; uint8_t
  // *des_addr; ... }. memcpy(macPod, mac, 6) copiava quindi il puntatore
  // src_addr (4 byte) + meta' di des_addr (2 byte) e li usava come MAC.
  // Da qui il fantomatico "7A:EE:21:3C:74:EE": un indirizzo di MEMORIA,
  // non un dispositivo. Il Visore trasmetteva verso il nulla -> il POD non
  // riceveva mai nulla (fw_visore='n.d.' a DB dal 12/07), pur rispondendo
  // regolarmente con ACK a livello MAC (log POD 2.4.3: ok=39 fail=3).
  //
  // FIX: non apprendere nulla. macPod[] contiene gia' il BSSID dell'AP del
  // POD, letto da trovaPodSegugio() via scan di AYE_POD_NET (= CE:BA:97:
  // 1C:BB:F0, confermato dal log POD 2.4.3), e il peer e' gia' registrato
  // in setupRadioESPNOW(). Il parametro 'mac' non va MAI dereferenziato.
  if (len == sizeof(struct_nautica)) {
    memcpy(&datiNautici, incomingData, sizeof(datiNautici));
    nuoviDatiRicevuti = true;
    ultimoPacchetto   = millis();
    tempoLinkPerso    = 0;
    canaleTrovato     = true;
  } else {
    Serial.printf("[RADIO-WARN] size mismatch: recv=%d expected=%d\n",
                  len, sizeof(struct_nautica));
  }
}

void setupRadioESPNOW(){
  WiFi.mode(WIFI_STA);WiFi.disconnect();delay(100);
  esp_wifi_set_promiscuous(true);esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);esp_wifi_set_promiscuous(false);
  if(esp_now_init()!=ESP_OK)return;
  // ATTENZIONE: il cast (esp_now_recv_cb_t) è latente su ESP32 Arduino core 3.x
  // (firma cambiata da uint8_t* a esp_now_recv_info_t*). Sicuro in V45.5.18
  // perché OnDataRecv() NON dereferenzia il parametro 'mac' — usa solo len e
  // incomingData. NON aggiungere letture di mac/info senza aggiornare la firma.
  // Fix definitivo pianificato in V45.6.0 (OTA). (Task 4 — V45.5.18)
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  esp_now_peer_info_t p={};memcpy(p.peer_addr,macPod,6);p.channel=0;p.encrypt=false;esp_now_add_peer(&p);
  Serial.printf("[SYS] ESP-NOW OK. sizeof(struct_nautica)=%d sizeof(datiDaInviare)=%d\n",sizeof(struct_nautica),sizeof(datiDaInviare));
}

void inviaDatiAlPod(){
  datiDaInviare.batteriaVisore=(float)leggiBatteriaStabilizzata();
  strncpy(datiDaInviare.fwVisore,FW_VERSION,10);
  // V45.5.14: il valore di ritorno era ignorato -> il TX rotto era invisibile.
  // ESP_ERR_ESPNOW_NOT_FOUND (0x3067) = MAC non in peer list.
  esp_err_t r = esp_now_send(macPod,(uint8_t*)&datiDaInviare,sizeof(datiDaInviare));
  if (r != ESP_OK) {
    static unsigned long lastErrLog = 0;
    if (millis() - lastErrLog > 5000) {   // rate-limit: 1 log ogni 5s
      lastErrLog = millis();
      Serial.printf("[RADIO-ERR] esp_now_send=%d verso "
                    "%02X:%02X:%02X:%02X:%02X:%02X%s\n", (int)r,
                    macPod[0],macPod[1],macPod[2],macPod[3],macPod[4],macPod[5],
                    (r == ESP_ERR_ESPNOW_NOT_FOUND) ? " (peer non registrato)" : "");
    }
  }
}

// ── UI BUILD (invariato da V45.5.11, eccetto aggiunta screen_wp_nav) ──────
void applica_sfondo_blindato(lv_obj_t *p){
  lv_obj_t *bg=lv_obj_create(p);lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg,400,300);
  lv_obj_set_style_bg_color(bg,lv_color_white(),0);
  lv_obj_set_style_bg_opa(bg,LV_OPA_COVER,0);
}

void crea_top_bar(lv_obj_t *p, lv_obj_t **lg, lv_obj_t **lc, lv_obj_t **lb){
  lv_obj_t *b=lv_obj_create(p);lv_obj_remove_style_all(b);
  lv_obj_set_size(b,400,25);
  lv_obj_set_style_bg_color(b,lv_color_black(),0);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
  *lg=lv_label_create(b);lv_label_set_text(*lg,"GPS: --");
  lv_obj_set_style_text_color(*lg,lv_color_white(),0);lv_obj_set_style_text_font(*lg,&lv_font_montserrat_14,0);
  lv_obj_align(*lg,LV_ALIGN_LEFT_MID,5,0);
  *lc=lv_label_create(b);lv_label_set_text(*lc,"CLOUD: --");
  lv_obj_set_style_text_color(*lc,lv_color_white(),0);lv_obj_set_style_text_font(*lc,&lv_font_montserrat_14,0);
  lv_obj_align(*lc,LV_ALIGN_CENTER,0,0);
  *lb=lv_label_create(b);lv_label_set_text(*lb,"V: --%");
  lv_obj_set_style_text_color(*lb,lv_color_white(),0);lv_obj_set_style_text_font(*lb,&lv_font_montserrat_14,0);
  lv_obj_align(*lb,LV_ALIGN_RIGHT_MID,-5,0);
}

void crea_bot_bar(lv_obj_t *p, lv_obj_t **lo){
  lv_obj_t *b=lv_obj_create(p);lv_obj_remove_style_all(b);
  lv_obj_set_size(b,400,25);lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,0,0);
  lv_obj_set_style_bg_color(b,lv_color_black(),0);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
  *lo=lv_label_create(b);lv_label_set_text(*lo,"--:--");
  lv_obj_set_style_text_color(*lo,lv_color_white(),0);lv_obj_set_style_text_font(*lo,&lv_font_montserrat_14,0);
  lv_obj_align(*lo,LV_ALIGN_CENTER,0,0);
}

void crea_quadrante(lv_obj_t *par,int x,int y,int w,int h,const char* t,const char* u,lv_obj_t **lv){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,w,h);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,y);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,2,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);lv_obj_set_style_text_font(lt,&lv_font_montserrat_16,0);lv_obj_align(lt,LV_ALIGN_TOP_LEFT,5,5);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--");lv_obj_set_style_text_font(*lv,&lv_font_montserrat_32,0);lv_obj_align(*lv,LV_ALIGN_CENTER,0,8);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);lv_obj_set_style_text_font(lu,&lv_font_montserrat_12,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_RIGHT,-5,-5);
}
void crea_quadrante_80(lv_obj_t *par,int x,int y,int w,int h,const char* t,const char* u,lv_obj_t **lv){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,w,h);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,y);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,2,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_set_style_clip_corner(p,true,0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);lv_obj_set_style_text_font(lt,&lv_font_montserrat_12,0);lv_obj_align(lt,LV_ALIGN_TOP_LEFT,3,2);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--");lv_obj_set_style_text_font(*lv,&lv_font_stretto_80,0);lv_obj_align(*lv,LV_ALIGN_CENTER,0,5);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);lv_obj_set_style_text_font(lu,&lv_font_montserrat_12,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_RIGHT,-3,-2);
}

void crea_meta_schermo_80(lv_obj_t *par,int x,const char* t,const char* u,lv_obj_t **lv){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,200,250);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,25);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,3,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_set_style_clip_corner(p,true,0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);
  lv_obj_set_style_text_font(lt,&lv_font_montserrat_16,0);lv_obj_align(lt,LV_ALIGN_TOP_MID,0,4);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--");
  lv_obj_set_style_text_font(*lv,&lv_font_stretto_80,0);lv_obj_align(*lv,LV_ALIGN_CENTER,0,-5);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);
  lv_obj_set_style_text_font(lu,&lv_font_montserrat_32,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_MID,0,-4);
}
void crea_meta_schermo_80_nodec(lv_obj_t *par,int x,const char* t,const char* u,
                               lv_obj_t **lv, lv_obj_t **lv_dec){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,200,250);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,25);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,3,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_set_style_clip_corner(p,true,0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);
  lv_obj_set_style_text_font(lt,&lv_font_montserrat_16,0);lv_obj_align(lt,LV_ALIGN_TOP_MID,0,4);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--");
  lv_obj_set_style_text_font(*lv,&lv_font_stretto_80,0);
  lv_obj_align(*lv,LV_ALIGN_CENTER,-20,-5);
  *lv_dec=lv_label_create(p);lv_label_set_text(*lv_dec,".0");
  lv_obj_set_style_text_font(*lv_dec,&lv_font_montserrat_32,0);
  lv_obj_align(*lv_dec,LV_ALIGN_CENTER,45,25);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);
  lv_obj_set_style_text_font(lu,&lv_font_montserrat_32,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_MID,0,-4);
}
void crea_meta_schermo_160(lv_obj_t *par,int x,const char* t,const char* u,lv_obj_t **lv){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,200,250);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,25);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,3,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_set_style_clip_corner(p,true,0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);
  lv_obj_set_style_text_font(lt,&lv_font_montserrat_16,0);lv_obj_align(lt,LV_ALIGN_TOP_MID,0,4);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--");
  lv_obj_set_style_text_font(*lv,&lv_font_stretto_160,0);lv_obj_align(*lv,LV_ALIGN_CENTER,0,-5);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);
  lv_obj_set_style_text_font(lu,&lv_font_montserrat_14,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_MID,0,-4);
}
void crea_meta_schermo_192dec(lv_obj_t *par,int x,const char* t,const char* u,lv_obj_t **lv){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,200,250);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,25);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,3,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_set_style_clip_corner(p,true,0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);
  lv_obj_set_style_text_font(lt,&lv_font_montserrat_16,0);lv_obj_align(lt,LV_ALIGN_TOP_MID,0,4);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--.--");
  lv_obj_set_style_text_font(*lv,&lv_font_stretto_160,0);lv_obj_align(*lv,LV_ALIGN_CENTER,0,10);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);
  lv_obj_set_style_text_font(lu,&lv_font_montserrat_14,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_MID,0,-4);
}
void crea_meta_schermo_192_nodec(lv_obj_t *par, int x, const char* t, const char* u,
                                 lv_obj_t **lv, lv_obj_t **lv_dec){
  lv_obj_t *p = lv_obj_create(par); lv_obj_remove_style_all(p); lv_obj_set_size(p, 200, 250); lv_obj_align(p, LV_ALIGN_TOP_LEFT, x, 25);
  lv_obj_set_style_bg_color(p, lv_color_white(), 0); lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(p, 3, 0); lv_obj_set_style_border_color(p, lv_color_black(), 0);
  lv_obj_set_style_clip_corner(p, true, 0);
  lv_obj_t *lt = lv_label_create(p); lv_label_set_text(lt, t);
  lv_obj_set_style_text_font(lt, &lv_font_montserrat_16, 0); lv_obj_align(lt, LV_ALIGN_TOP_MID, 0, 4);
  *lv = lv_label_create(p); lv_label_set_text(*lv, "--");
  lv_obj_set_style_text_font(*lv, &lv_font_stretto_160, 0);
  lv_obj_align(*lv, LV_ALIGN_CENTER, -10, 5);
  *lv_dec = lv_label_create(p); lv_label_set_text(*lv_dec, ".0");
  lv_obj_set_style_text_font(*lv_dec, &lv_font_stretto_80, 0);
  lv_obj_align_to(*lv_dec, *lv, LV_ALIGN_OUT_RIGHT_BOTTOM, 20, 0);
  lv_obj_t *lu = lv_label_create(p); lv_label_set_text(lu, u);
  lv_obj_set_style_text_font(lu, &lv_font_montserrat_14, 0); lv_obj_align(lu, LV_ALIGN_BOTTOM_MID, 0, -4);
}
void crea_meta_schermo_192(lv_obj_t *par,int x,const char* t,const char* u,lv_obj_t **lv){
  lv_obj_t *p=lv_obj_create(par);lv_obj_remove_style_all(p);lv_obj_set_size(p,200,250);lv_obj_align(p,LV_ALIGN_TOP_LEFT,x,25);
  lv_obj_set_style_bg_color(p,lv_color_white(),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(p,3,0);lv_obj_set_style_border_color(p,lv_color_black(),0);
  lv_obj_set_style_clip_corner(p,true,0);
  lv_obj_t *lt=lv_label_create(p);lv_label_set_text(lt,t);
  lv_obj_set_style_text_font(lt,&lv_font_montserrat_16,0);lv_obj_align(lt,LV_ALIGN_TOP_MID,0,4);
  *lv=lv_label_create(p);lv_label_set_text(*lv,"--");
  lv_obj_set_style_text_font(*lv,&lv_font_stretto_160,0);lv_obj_align(*lv,LV_ALIGN_CENTER,0,-5);
  lv_obj_t *lu=lv_label_create(p);lv_label_set_text(lu,u);
  lv_obj_set_style_text_font(lu,&lv_font_montserrat_14,0);lv_obj_align(lu,LV_ALIGN_BOTTOM_MID,0,-4);
}

void costruisci_interfaccia(){
  // ── pag 0: screen_main (invariata) ───────────────────────────────────
  screen_main=lv_obj_create(NULL);applica_sfondo_blindato(screen_main);
  crea_top_bar(screen_main,&lbl_gps_m,&lbl_cloud_m,&lbl_bat_m);
  crea_bot_bar(screen_main,&lbl_ora_m);
  lv_obj_t *cm=lv_obj_create(screen_main);lv_obj_remove_style_all(cm);lv_obj_set_size(cm,400,250);lv_obj_align(cm,LV_ALIGN_TOP_LEFT,0,25);
  crea_quadrante_80(cm,0,  0,200,83,"HDG","deg",&val_hdg);
  crea_quadrante_80(cm,200,0,200,83,"SOG",    "kn", &val_sog);
  crea_quadrante_80(cm,0,  80,200,85,"AWA",   "deg",&val_awa);
  { lv_obj_t *par=lv_obj_get_parent(val_awa);
    val_awa_dir_m=lv_label_create(par);
    lv_label_set_text(val_awa_dir_m,"");
    lv_obj_set_style_text_font(val_awa_dir_m,&lv_font_montserrat_32,0);
    lv_obj_align(val_awa_dir_m,LV_ALIGN_RIGHT_MID,-4,0); }
  crea_quadrante_80(cm,200,80,200,85,"AWS",   "kn", &val_aws);
  crea_quadrante_80(cm,0,  165,200,85,"TWD",  "deg",&val_twd);
  crea_quadrante_80(cm,200,165,200,85,"TWS",  "kn", &val_tws);

  // ── pag 1: screen_maxi (invariata) ───────────────────────────────────
  screen_maxi=lv_obj_create(NULL);applica_sfondo_blindato(screen_maxi);
  crea_top_bar(screen_maxi,&lbl_gps_maxi,&lbl_cloud_maxi,&lbl_bat_maxi);
  crea_bot_bar(screen_maxi,&lbl_ora_maxi);
  crea_meta_schermo_192_nodec(screen_maxi,   0,"SOG","kn", &val_maxi_sog, &val_maxi_sog_dec);
  crea_meta_schermo_192(screen_maxi, 200,"HDG","deg",&val_maxi_hdg);

  // ── pag 2: screen_giant_wind (invariata) ─────────────────────────────
  screen_giant_wind=lv_obj_create(NULL);applica_sfondo_blindato(screen_giant_wind);
  crea_top_bar(screen_giant_wind,&lbl_gps_gw,&lbl_cloud_gw,&lbl_bat_gw);
  crea_bot_bar(screen_giant_wind,&lbl_ora_gw);
  crea_meta_schermo_192_nodec(screen_giant_wind, 0,"TWS","kn", &val_g_tws, &val_g_tws_dec);
  crea_meta_schermo_192(screen_giant_wind, 200,"AWA","deg",&val_g_awa);
  val_g_awa_dir=lv_label_create(lv_obj_get_parent(val_g_awa));
  lv_label_set_text(val_g_awa_dir,"");
  lv_obj_set_style_text_font(val_g_awa_dir,&lv_font_montserrat_32,0);
  lv_obj_align(val_g_awa_dir,LV_ALIGN_BOTTOM_RIGHT,-6,-4);

  // ── pag 3: screen_giant_wp (invariata) ───────────────────────────────
  screen_giant_wp=lv_obj_create(NULL);applica_sfondo_blindato(screen_giant_wp);
  crea_top_bar(screen_giant_wp,&lbl_gps_gwp,&lbl_cloud_gwp,&lbl_bat_gwp);
  crea_bot_bar(screen_giant_wp,&lbl_ora_gwp);
  crea_meta_schermo_192_nodec(screen_giant_wp, 0,"VMG","kn", &val_g_vmg, &val_g_vmg_dec);
  crea_meta_schermo_192(screen_giant_wp, 200,"TWD","deg",&val_g_twd);

  // ── pag 4: screen_wp_nav (NUOVA V45.5.12) ────────────────────────────
  // Navigazione waypoint: DTW (sinistra) | BTW (destra)
  // Bot bar: ETA mm:ss + VMG verso WP
  // Top bar cloud: "ARRIVED!" quando wp_arrive_alert
  // val_wp_rel: bearing relativo prora (+° D / -° S) — piccolo in basso
  screen_wp_nav=lv_obj_create(NULL);applica_sfondo_blindato(screen_wp_nav);
  crea_top_bar(screen_wp_nav,&lbl_gps_wpn,&lbl_cloud_wpn,&lbl_bat_wpn);
  crea_bot_bar(screen_wp_nav,&lbl_ora_wpn);
  // DTW: stretto_192 intero + stretto_80 decimale (2 dec)
  crea_meta_schermo_192_nodec(screen_wp_nav, 0,"DTW","nm", &val_wp_dtw, &val_wp_dtw_dec);
  // BTW: stretto_192 intero
  crea_meta_schermo_192(screen_wp_nav, 200,"BTW","deg",&val_wp_btw);
  // WP_REL: label piccola sotto il DTW — bearing relativo alla prora
  { lv_obj_t *par_dtw = lv_obj_get_parent(val_wp_dtw);
    val_wp_rel = lv_label_create(par_dtw);
    lv_label_set_text(val_wp_rel, "REL --");
    lv_obj_set_style_text_font(val_wp_rel, &lv_font_montserrat_16, 0);
    lv_obj_align(val_wp_rel, LV_ALIGN_BOTTOM_LEFT, 6, -2); }

  // ── pag 5: screen_heel_trim (era pag 4 — invariata) ──────────────────
  screen_heel_trim=lv_obj_create(NULL);applica_sfondo_blindato(screen_heel_trim);
  crea_top_bar(screen_heel_trim,&lbl_gps_ht,&lbl_cloud_ht,&lbl_bat_ht);
  crea_bot_bar(screen_heel_trim,&lbl_ora_ht);
  crea_meta_schermo_192(screen_heel_trim,  0,"HEEL","deg",&val_heel);
  crea_meta_schermo_192(screen_heel_trim,200,"TRIM","deg",&val_trim);

  // ── pag 6: screen_session (era pag 5 — invariata) ────────────────────
  screen_session=lv_obj_create(NULL);applica_sfondo_blindato(screen_session);
  crea_top_bar(screen_session,&lbl_gps_s,&lbl_cloud_s,&lbl_bat_s);
  crea_bot_bar(screen_session,&lbl_ora_s);
  label_session_instr=lv_label_create(screen_session);
  lv_label_set_text(label_session_instr,
    "AVVIA NAVIGAZIONE\n\n"
    "tieni premuto 3 secondi\n"
    "il tasto PAGE\n"
    "per avviare la sessione!\n\n"
    "Buon Vento!");
  lv_obj_set_style_text_align(label_session_instr,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_font(label_session_instr,&lv_font_montserrat_32,0);
  lv_obj_align(label_session_instr,LV_ALIGN_CENTER,0,0);
  cont_session_data=lv_obj_create(screen_session);lv_obj_remove_style_all(cont_session_data);
  lv_obj_set_size(cont_session_data,400,250);lv_obj_align(cont_session_data,LV_ALIGN_TOP_LEFT,0,25);
  lv_obj_add_flag(cont_session_data,LV_OBJ_FLAG_HIDDEN);
  crea_quadrante_80(cont_session_data,0,  0,200,80,"TIME",    "h:m:s",&val_s_time);
  crea_quadrante_80(cont_session_data,200,0,200,80,"DISTANCE","nm",   &val_s_dist);
  crea_quadrante_80(cont_session_data,0,  80,200,85,"HEADING","deg",  &val_s_hdg);
  crea_quadrante_80(cont_session_data,200,80,200,85,"SOG",    "kn",   &val_s_roll);
  crea_quadrante_80(cont_session_data,0,  165,200,85,"AWA",   "deg",  &val_s_awa);
  crea_quadrante_80(cont_session_data,200,165,200,85,"TWS",   "kn",   &val_s_aws);
  label_countdown=lv_label_create(screen_session);
  lv_obj_set_style_text_font(label_countdown,&lv_font_montserrat_48,0);
  lv_obj_align(label_countdown,LV_ALIGN_CENTER,0,0);lv_obj_add_flag(label_countdown,LV_OBJ_FLAG_HIDDEN);
  label_start_flash=lv_label_create(screen_session);
  lv_label_set_text(label_start_flash,"START!");
  lv_obj_set_style_text_font(label_start_flash,&lv_font_montserrat_48,0);
  lv_obj_align(label_start_flash,LV_ALIGN_CENTER,0,0);lv_obj_add_flag(label_start_flash,LV_OBJ_FLAG_HIDDEN);

  // ── pag 7: screen_summary (era pag 6 — invariata) ────────────────────
  screen_summary=lv_obj_create(NULL);applica_sfondo_blindato(screen_summary);
  crea_top_bar(screen_summary,&lbl_gps_sum,&lbl_cloud_sum,&lbl_bat_sum);
  crea_bot_bar(screen_summary,&lbl_ora_sum);
  crea_quadrante(screen_summary,0,  25,200,80,"TIME",     "h:m:s",&val_sum_time);
  crea_quadrante(screen_summary,200,25,200,80,"DIST",     "nm",   &val_sum_dist);
  crea_quadrante(screen_summary,0,  105,200,80,"SOG Mx/Av","kn",  &val_sum_sog);
  crea_quadrante(screen_summary,200,105,200,80,"AWS Mx/Av","kn",  &val_sum_aws);
  lv_obj_set_style_text_font(val_sum_sog,&lv_font_montserrat_32,0);
  lv_obj_set_style_text_font(val_sum_aws,&lv_font_montserrat_32,0);
  label_upload_status=lv_label_create(screen_summary);
  lv_obj_set_style_text_font(label_upload_status,&lv_font_montserrat_32,0);
  lv_obj_align(label_upload_status,LV_ALIGN_CENTER,0,80);
  lv_label_set_text(label_upload_status,"ATTESA POD...");

  // ── pag 8: screen_info (era pag 7 — invariata) ───────────────────────
  screen_info=lv_obj_create(NULL);applica_sfondo_blindato(screen_info);
  crea_top_bar(screen_info,&lbl_gps_info,&lbl_cloud_info,&lbl_bat_info);
  crea_bot_bar(screen_info,&lbl_ora_info);
  lv_obj_t *pt=lv_obj_create(screen_info);lv_obj_remove_style_all(pt);
  lv_obj_set_size(pt,230,270);lv_obj_align(pt,LV_ALIGN_TOP_LEFT,8,28);
  lv_obj_t *it=lv_label_create(pt);lv_label_set_text(it,"AYE SYSTEM");
  lv_obj_set_style_text_font(it,&lv_font_montserrat_16,0);lv_obj_align(it,LV_ALIGN_TOP_LEFT,0,0);
  txt_info_pod=lv_label_create(pt);
  lv_label_set_text_fmt(txt_info_pod,"POD: %s",found_pod_ssid);
  lv_obj_set_style_text_font(txt_info_pod,&lv_font_montserrat_16,0);lv_obj_align(txt_info_pod,LV_ALIGN_TOP_LEFT,0,22);
  txt_info_capt=lv_label_create(pt);
  lv_label_set_text(txt_info_capt,"CAPT: Giorgio G.");
  lv_obj_set_style_text_font(txt_info_capt,&lv_font_montserrat_16,0);lv_obj_align(txt_info_capt,LV_ALIGN_TOP_LEFT,0,44);
  txt_fw_pod=lv_label_create(pt);
  lv_label_set_text(txt_fw_pod,"FW POD: ---");
  lv_obj_set_style_text_font(txt_fw_pod,&lv_font_montserrat_16,0);lv_obj_align(txt_fw_pod,LV_ALIGN_TOP_LEFT,0,66);
  lv_obj_t *fv=lv_label_create(pt);
  lv_label_set_text_fmt(fv,"FW VIS: %s",FW_VERSION);
  lv_obj_set_style_text_font(fv,&lv_font_montserrat_16,0);lv_obj_align(fv,LV_ALIGN_TOP_LEFT,0,88);
  txt_info_bat=lv_label_create(pt);
  lv_label_set_text(txt_info_bat,"BAT P:--%  V:--%");
  lv_obj_set_style_text_font(txt_info_bat,&lv_font_montserrat_16,0);
  lv_obj_align(txt_info_bat,LV_ALIGN_TOP_LEFT,0,110);
  txt_info_env=lv_label_create(pt);
  lv_label_set_text(txt_info_env,"T:-- \xC2\xB0""C  H:--%");
  lv_obj_set_style_text_font(txt_info_env,&lv_font_montserrat_16,0);
  lv_obj_align(txt_info_env,LV_ALIGN_TOP_LEFT,0,132);
  lv_obj_t *tp=lv_label_create(pt);lv_label_set_text(tp,"PIN CREW:");
  lv_obj_set_style_text_font(tp,&lv_font_montserrat_16,0);
  lv_obj_align(tp,LV_ALIGN_TOP_LEFT,0,162);
  txt_info_pin=lv_label_create(pt);
  lv_label_set_text(txt_info_pin,"----");
  lv_obj_set_style_text_font(txt_info_pin,&lv_font_montserrat_32,0);
  lv_obj_align(txt_info_pin,LV_ALIGN_TOP_LEFT,0,180);
  qr_img_obj=lv_img_create(screen_info);
  lv_img_set_src(qr_img_obj,&QR_AyeNautical_Mate);
  lv_obj_set_pos(qr_img_obj,272,50);
  lv_obj_t *is=lv_label_create(screen_info);
  lv_label_set_text(is,"Sali a bordo!");
  lv_obj_set_style_text_font(is,&lv_font_montserrat_14,0);
  lv_obj_set_pos(is,280,174);

  lv_scr_load(screen_main);
}

// QR statico — invariato
void aggiornaDatiSchermataInfo(){ /* QR statico, nessuna azione */ }

// ── NAVIGAZIONE PAGINE — V45.5.12 ─────────────────────────────────────────
// 0=main 1=SOG+HDG 2=TWS+AWA 3=VMG+TWD 4=DTW+BTW 5=HEEL+TRIM
// 6=session 7=summary 8=info
void toggle_page(){
  if(countdown_active) return;
  current_page++; if(current_page>8) current_page=0;  // era >7
  esegui_anti_ghosting();
  if(!Lvgl_lock(-1)) return;
  if     (current_page==0) lv_scr_load(screen_main);
  else if(current_page==1) lv_scr_load(screen_maxi);
  else if(current_page==2) lv_scr_load(screen_giant_wind);
  else if(current_page==3) lv_scr_load(screen_giant_wp);
  else if(current_page==4) lv_scr_load(screen_wp_nav);         // NUOVA
  else if(current_page==5) lv_scr_load(screen_heel_trim);      // era 4
  else if(current_page==6){                                      // era 5
    lv_scr_load(screen_session);
    if(!session_active){
      lv_obj_clear_flag(label_session_instr,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(cont_session_data,LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(label_session_instr,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(cont_session_data,LV_OBJ_FLAG_HIDDEN);
    }
  }
  else if(current_page==7) lv_scr_load(screen_summary);        // era 6
  else if(current_page==8){                                      // era 7
    lv_obj_invalidate(lv_scr_act());lv_timer_handler();Lvgl_unlock();
    lv_scr_load(screen_info);return;
  }
  lv_obj_invalidate(lv_scr_act());lv_timer_handler();Lvgl_unlock();
}

// ── SESSIONE (invariata da V45.5.11) ──────────────────────────────────────
void start_countdown(){
  session_max_sog=session_sum_sog=0;session_count_sog=0;
  session_max_aws=session_sum_aws=0;session_count_aws=0;
  countdown_active=true;countdown_val=5;countdown_last_tick=millis();
  esegui_anti_ghosting();
  if(!Lvgl_lock(-1))return;
  lv_obj_add_flag(label_session_instr,LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(label_countdown,LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(label_countdown,"5");
  lv_obj_invalidate(lv_scr_act());Lvgl_unlock();
}

void handle_countdown(){
  if(!countdown_active)return;
  if(millis()-countdown_last_tick>=1000){
    countdown_last_tick=millis();countdown_val--;
    if(!Lvgl_lock(-1))return;
    if(countdown_val>0)lv_label_set_text_fmt(label_countdown,"%d",countdown_val);
    else if(countdown_val==0){lv_obj_add_flag(label_countdown,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(label_start_flash,LV_OBJ_FLAG_HIDDEN);}
    else{lv_obj_add_flag(label_start_flash,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(cont_session_data,LV_OBJ_FLAG_HIDDEN);}
    lv_obj_invalidate(lv_scr_act());Lvgl_unlock();
    if(countdown_val<0){esegui_anti_ghosting();execute_start();}
  }
}


// ── V45.6.0: da comando a STATO ───────────────────────────────────────────
// execute_start()/stop_session() non "armano" piu' un comando da far
// consumare al POD: settano un LIVELLO che ogni pacchetto ripete finche'
// non cambia. Non serve piu' cmd_arm_time ne' il blocco di sgancio: se un
// pacchetto si perde, il successivo (100ms dopo) ridice la stessa cosa.
void execute_start(){
  countdown_active=false;session_active=true;session_distance=0;session_time_elapsed=0;last_calc_time=millis();
  datiDaInviare.sessione_attiva=1;datiDaInviare.session_dist=0.0f;inviaDatiAlPod();
  if(!Lvgl_lock(-1))return;lv_scr_load(screen_session);lv_obj_invalidate(lv_scr_act());Lvgl_unlock();
}

void stop_session(){
  session_active=false;
  session_avg_sog=session_count_sog>0?(session_sum_sog/session_count_sog):0;
  session_avg_aws=session_count_aws>0?(session_sum_aws/session_count_aws):0;

  // ⚠ FIX V45.6.0 — lo STATO si abbassa PRIMA di toccare LVGL.
  // Nella V45.5.22 il "cmd_sessione=2" stava dopo un "if(!Lvgl_lock(-1))return;":
  // se il lock falliva, la funzione usciva con session_active gia' false ma
  // senza aver mai comunicato lo stop al POD → sessione aperta a DB per
  // sempre, con il Visore convinto di averla chiusa. Ora lo stato e la sua
  // trasmissione non dipendono piu' dalla UI.
  // (Con la logica a livello sarebbe comunque recuperabile — il pacchetto
  //  successivo direbbe 0 — ma non c'e' motivo di lasciare il buco.)
  datiDaInviare.sessione_attiva=0;
  datiDaInviare.session_dist=session_distance;
  inviaDatiAlPod();

  // V45.6.1: arma la finestra di grazia anti-scan. Da qui il POD impieghera'
  // qualche secondo (POST telemetria + RPC chiusura = 2 handshake TLS) prima
  // di tornare a trasmettere: in questo intervallo il Visore non deve
  // scansionare, o si sposta di canale e perde il POD (bug 16/07).
  t_fine_sessione = millis();
  if(t_fine_sessione == 0) t_fine_sessione = 1;  // 0 e' il sentinella "nessuno stop"

  pending_cloud_upload=true;current_page=7;  // era 6

  unsigned long t=session_time_elapsed/1000;int h=t/3600,m=(t%3600)/60,s=t%60;
  String ts=(h>0?String(h)+":":"")+( m<10?"0":"")+String(m)+":"+(s<10?"0":"")+String(s);
  if(!Lvgl_lock(-1))return;
  lv_label_set_text(val_sum_time,ts.c_str());
  lv_label_set_text(val_sum_dist,String(session_distance,2).c_str());
  lv_label_set_text(val_sum_sog,(String(session_max_sog,1)+" / "+String(session_avg_sog,1)).c_str());
  lv_label_set_text(val_sum_aws,(String(session_max_aws,1)+" / "+String(session_avg_aws,1)).c_str());
  lv_obj_add_flag(cont_session_data,LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(label_session_instr,LV_OBJ_FLAG_HIDDEN);Lvgl_unlock();

  if(!Lvgl_lock(-1))return;lv_scr_load(screen_summary);lv_obj_invalidate(lv_scr_act());Lvgl_unlock();
}

// Pulsante: 3s su pagina 6 (era 5) per avvio/stop sessione
void read_button(){
  static unsigned long ld=0;static bool lr=HIGH;bool raw=digitalRead(VISORE_BTN_PIN);
  if(raw!=lr)ld=millis();lr=raw;
  if((millis()-ld)>50){bool pr=(raw==LOW);
    if(pr&&!btn_state){btn_state=true;btn_press_time=millis();}
    else if(!pr&&btn_state){btn_state=false;unsigned long dur=millis()-btn_press_time;
      if(dur>=3000&&current_page==6){if(!session_active&&!countdown_active)start_countdown();else if(session_active)stop_session();}  // era pag 5
      else if(dur>50)toggle_page();
    }
  }
}

// ── AGGIORNA UI — V45.5.12 ────────────────────────────────────────────────
void aggiornaUI(){
  if(!Lvgl_lock(-1)) return;

  // ── pag 0: screen_main (invariata) ───────────────────────────────────
  lv_label_set_text(val_hdg,String(datiNautici.heading).c_str());
  { float sogV=datiNautici.sog;
    lv_label_set_text(val_sog,(sogV<1.0f?"0.0":String(sogV,1)).c_str()); }
  if(datiNautici.aws<0.05f||datiNautici.awa<0){
    lv_label_set_text(val_awa,"--");lv_label_set_text(val_awa_dir_m,"");
    lv_label_set_text(val_aws,"0.0");
    lv_label_set_text(val_s_awa,"--");lv_label_set_text(val_s_aws,"0.0");
  } else {
    float ar=datiNautici.awa;if(ar>180)ar-=360;
    lv_label_set_text(val_awa,String((int)fabsf(ar)).c_str());
    lv_label_set_text(val_awa_dir_m, ar>=0 ? "D" : "S");
    lv_label_set_text(val_aws,(datiNautici.aws<0.05f?"0.0":String(datiNautici.aws,1)).c_str());
    lv_label_set_text(val_s_awa,String((int)fabsf(ar)).c_str());
    lv_label_set_text(val_s_aws,String(datiNautici.aws,1).c_str());
  }
  if(datiNautici.tws<0.05f){lv_label_set_text(val_tws,"0.0");lv_label_set_text(val_twd,"--");}
  else{lv_label_set_text(val_tws,String(datiNautici.tws,1).c_str());
       lv_label_set_text(val_twd,datiNautici.twd<0?"--":String((int)datiNautici.twd).c_str());}

  // ── pag 1: SOG | HDG (invariata) ─────────────────────────────────────
  { float sogV=datiNautici.sog;
    int sv_int=(int)sogV;
    int sv_dec=(int)(fabsf(sogV-sv_int)*10);
    lv_label_set_text(val_maxi_sog,(sogV<1.0f?"0":String(sv_int)).c_str());
    lv_label_set_text(val_maxi_sog_dec,("."+String(sogV<1.0f?0:sv_dec)).c_str()); }
  lv_label_set_text(val_maxi_hdg,String(datiNautici.heading).c_str());

  // ── pag 2: TWS | AWA (invariata) ─────────────────────────────────────
  { float tv=datiNautici.tws;
    lv_label_set_text(val_g_tws,String((int)tv).c_str());
    lv_label_set_text(val_g_tws_dec,("."+String((int)(fabsf(tv-(int)tv)*10))).c_str()); }
  { float ar=datiNautici.awa;if(ar>180)ar-=360;
    lv_label_set_text(val_g_awa,String((int)fabsf(ar)).c_str());
    lv_label_set_text(val_g_awa_dir, ar>=0 ? "D" : "S"); }

  // ── pag 3: VMG | TWD (invariata) ─────────────────────────────────────
  { float vv=datiNautici.vmg_wind;
    lv_label_set_text(val_g_vmg,String((int)vv).c_str());
    lv_label_set_text(val_g_vmg_dec,("."+String((int)(fabsf(vv-(int)vv)*10))).c_str()); }
  lv_label_set_text(val_g_twd,datiNautici.twd<0?"--":String((int)datiNautici.twd).c_str());

  // ── pag 4: DTW | BTW (NUOVA V45.5.12) ───────────────────────────────
  {
    float dtw = datiNautici.dtw;
    float btw = datiNautici.btw;
    float rel = datiNautici.wp_bearing_rel;
    bool  wp_valido = (dtw > 0.001f);

    if (!wp_valido) {
      lv_label_set_text(val_wp_dtw,     "---");
      lv_label_set_text(val_wp_dtw_dec, "");
      lv_label_set_text(val_wp_btw,     "---");
      lv_label_set_text(val_wp_rel,     "REL --");
      lv_label_set_text(lbl_cloud_wpn,  "Nessun WP");
      lv_label_set_text(lbl_ora_wpn,    "Nessun waypoint attivo");
    } else if (datiNautici.wp_arrive_alert) {
      lv_label_set_text(val_wp_dtw,     "0");
      lv_label_set_text(val_wp_dtw_dec, ".0");
      lv_label_set_text(val_wp_btw,     String((int)roundf(btw)).c_str());
      lv_label_set_text(val_wp_rel,     "ARRIVED!");
      lv_label_set_text(lbl_cloud_wpn,  "** ARRIVED! **");
      lv_label_set_text(lbl_ora_wpn,    "*** WAYPOINT RAGGIUNTO ***");
    } else {
      // DTW con 1 decimale (era 2)
      int dtw_int = (int)dtw;
      int dtw_dec = (int)(fabsf(dtw - dtw_int) * 10);   // ← * 10 = 1 cifra
        lv_label_set_text(val_wp_dtw,     String(dtw_int).c_str());
      lv_label_set_text(val_wp_dtw_dec, ("." + String(dtw_dec)).c_str());
        lv_label_set_text(val_wp_btw,     String((int)roundf(btw)).c_str());
        // WP_REL: +12° D oppure -8° S
      const char* dir = (rel >= 0) ? "D" : "S";
      String rel_str = (rel >= 0 ? "+" : "") + String((int)roundf(rel)) + "\xB0 " + dir;
      lv_label_set_text(val_wp_rel, rel_str.c_str());
        // Bot bar: ETA + VMG
      String bot = "ETA " + formattaETA(datiNautici.eta_wp_sec) +
             "  VMG " + String(datiNautici.vmg_wp, 1) + "kn";
        lv_label_set_text(lbl_ora_wpn, bot.c_str());
    }
  }

  // ── pag 5: HEEL | TRIM (invariata) ───────────────────────────────────
  lv_label_set_text(val_heel,String(abs(datiNautici.roll)).c_str());
  lv_label_set_text(val_trim,String(abs(datiNautici.pitch)).c_str());

  // ── pag 6: Sessione dati (invariata) ─────────────────────────────────
  { unsigned long t=session_time_elapsed/1000;int h=t/3600,m=(t%3600)/60,s=t%60;
    String ts=(h>0?String(h)+":":"")+( m<10?"0":"")+String(m)+":"+(s<10?"0":"")+String(s);
    lv_label_set_text(val_s_time,ts.c_str());lv_label_set_text(val_s_dist,String(session_distance,2).c_str());
    lv_label_set_text(val_s_hdg,String(datiNautici.heading).c_str()); }
  { float sv=datiNautici.sog; lv_label_set_text(val_s_roll,(sv<1.0f?"0":String((int)round(sv))).c_str()); }
  lv_label_set_text(val_s_aws,String((int)round(datiNautici.tws)).c_str());

  // ── Screen info: FW + batterie (invariato) ────────────────────────────
  if(datiNautici.fw_str[0]!='\0'&&datiNautici.fw_str[0]!=' ')
    lv_label_set_text_fmt(txt_fw_pod,"FW POD: %s",datiNautici.fw_str);
  if(datiNautici.codice_crew[0]!='\0'&&datiNautici.codice_crew[0]!='-')
    lv_label_set_text(txt_info_pin,datiNautici.codice_crew);
  lv_label_set_text_fmt(txt_info_bat,"BAT P:%d%% V:%d%%",
    datiNautici.batteria_pod, leggiBatteriaStabilizzata());
  if(shtc3_found){
    sensors_event_t he,te;shtc3.getEvent(&he,&te);
    float temp_c = te.temperature;
    float hum_pct = he.relative_humidity;
    bool temp_ok = !isnan(temp_c) && temp_c > -40.0f && temp_c < 80.0f;
    bool hum_ok  = !isnan(hum_pct) && hum_pct >= 0.0f && hum_pct <= 100.0f;
    if(temp_ok && hum_ok)
      lv_label_set_text_fmt(txt_info_env,"T:%d \xC2\xB0""C  H:%d%%",(int)round(temp_c),(int)round(hum_pct));
    else if(temp_ok)
      lv_label_set_text_fmt(txt_info_env,"T:%d \xC2\xB0""C  H:--",(int)round(temp_c));
    else
      lv_label_set_text(txt_info_env,"T:--  H:--");
  } else lv_label_set_text(txt_info_env,"T:--  H:--");

  // ── Top/Bot bar — 9 schermate in V45.5.12 ────────────────────────────
  String gps_s = datiNautici.gps_fix ? "GPS:FIX" : "GPS:NO";
  String cld_s = session_active ? "REC" : (datiNautici.cloud_connesso ? "CLOUD:OK" : "CLOUD:NO");
  String bat_s = "V:" + String(leggiBatteriaStabilizzata()) + "%";
  String ora_s;
  if     (datiNautici.anchor_alert) ora_s = "** ANCORA ALERT **";
  else if(session_active)           ora_s = "Sessione in registrazione";
  else                              ora_s = getOraCET();

  // Barre tutte le schermate (9 elementi — aggiunta wpn a indice 4)
  lv_obj_t *gl[]={lbl_gps_m, lbl_gps_maxi,lbl_gps_gw, lbl_gps_gwp,
                  lbl_gps_wpn, lbl_gps_ht, lbl_gps_s,  lbl_gps_sum, lbl_gps_info};
  lv_obj_t *cl[]={lbl_cloud_m,lbl_cloud_maxi,lbl_cloud_gw,lbl_cloud_gwp,
                  lbl_cloud_wpn,lbl_cloud_ht,lbl_cloud_s,lbl_cloud_sum,lbl_cloud_info};
  lv_obj_t *bl[]={lbl_bat_m, lbl_bat_maxi,lbl_bat_gw, lbl_bat_gwp,
                  lbl_bat_wpn, lbl_bat_ht, lbl_bat_s,  lbl_bat_sum, lbl_bat_info};
  lv_obj_t *ol[]={lbl_ora_m, lbl_ora_maxi,lbl_ora_gw, lbl_ora_gwp,
                  lbl_ora_wpn, lbl_ora_ht, lbl_ora_s,  lbl_ora_sum, lbl_ora_info};
  for(int i=0;i<9;i++){
    lv_label_set_text(gl[i],gps_s.c_str());
    // cloud pag WP (i==4): già gestito sopra in blocco wp_valido/arrive
    // qui aggiorniamo solo le schermate non-WP
    if(i!=4) lv_label_set_text(cl[i],cld_s.c_str());
    lv_label_set_text(bl[i],bat_s.c_str());
    if(i!=4) lv_label_set_text(ol[i],ora_s.c_str());
  }

  Lvgl_unlock();
}

// ── SETUP & LOOP ─────────────────────────────────────────────────────────
void setup(){
  Serial.begin(115200);pinMode(VISORE_BTN_PIN,INPUT_PULLUP);
  Serial.printf("[BOOT] %s | struct_nautica=%d (atteso 108) | datiDaInviare=%d (atteso 22)\n",
    FW_VERSION,sizeof(struct_nautica),sizeof(datiDaInviare));
  if(sizeof(struct_nautica)!=108)
    Serial.println("[BOOT] *** ERRORE struct size! Allineare POD e Visore ***");
  setupDisplayHardware();
  setupSensoriLocali();
  trovaPodSegugio();
  setupRadioESPNOW();
  RealQr::init_qr_tables();
  if(Lvgl_lock(-1)){costruisci_interfaccia();Lvgl_unlock();}
}

void loop(){
  scansionaCanaliRadio();lv_timer_handler();delay(5);
  static unsigned long lhb=0;if(millis()-lhb>5000){Serial.println("[SYS] alive");lhb=millis();}

  // Aggiorna ora CET ogni 10s — V45.5.12: ol[] a 9 elementi, wpn separato
  static unsigned long lastOraUpdate=0;
  if(millis()-lastOraUpdate>10000){
    lastOraUpdate=millis();
    String ora_s;
    if     (datiNautici.anchor_alert) ora_s="** ANCORA ALERT **";
    else if(session_active)           ora_s="Sessione in registrazione";
    else                              ora_s=getOraCET();
    if(Lvgl_lock(-1)){
      // Aggiorna le 8 schermate standard (esclusa pag 4 WP)
      lv_obj_t *ol[]={lbl_ora_m,lbl_ora_maxi,lbl_ora_gw,lbl_ora_gwp,
                      lbl_ora_ht,lbl_ora_s,lbl_ora_sum,lbl_ora_info};
      for(int i=0;i<8;i++) lv_label_set_text(ol[i],ora_s.c_str());
      // Pag 4 WP: aggiorna ETA separatamente
      if(datiNautici.wp_arrive_alert)
        lv_label_set_text(lbl_ora_wpn,"*** WAYPOINT RAGGIUNTO ***");
      else if(datiNautici.dtw > 0.001f)
        lv_label_set_text(lbl_ora_wpn,
          ("ETA "+formattaETA(datiNautici.eta_wp_sec)+
           "  VMG "+String(datiNautici.vmg_wp,1)+"kn").c_str());
      else
        lv_label_set_text(lbl_ora_wpn,"Nessun waypoint attivo");
      Lvgl_unlock();
    }
  }

  read_button();handle_countdown();
  if(session_active){
    unsigned long now=millis(),dt=now-last_calc_time;last_calc_time=now;
    session_time_elapsed+=dt;session_distance+=(datiNautici.sog*dt)/3600000.0f;
    static unsigned long ls=0;if(now-ls>=1000){ls=now;
      if(datiNautici.sog>session_max_sog)session_max_sog=datiNautici.sog;session_sum_sog+=datiNautici.sog;session_count_sog++;
      if(datiNautici.aws>session_max_aws)session_max_aws=datiNautici.aws;session_sum_aws+=datiNautici.aws;session_count_aws++;}
  }
  // ── V45.6.0: latch RIMOSSO ──────────────────────────────────────────────
  // Il blocco "if(cmd_sessione!=0 && millis()-cmd_arm_time>8000) cmd=0"
  // non esiste piu'. Serviva a tenere armato un FRONTE per ~80 pacchetti
  // sperando che almeno uno arrivasse, e poi a sganciarlo per non farlo
  // riconsumare. Con lo STATO non c'e' niente da armare ne' da sganciare:
  // sessione_attiva vale 1 finche' la sessione e' attiva, 0 quando non lo e'.
  // ────────────────────────────────────────────────────────────────────────

  if(pending_cloud_upload){
    // V45.6.0: la conferma non puo' piu' basarsi su "cmd tornato a 0" (il
    // livello resta 0 stabilmente dopo lo stop, non e' piu' un evento che
    // si consuma). Ora si attende un pacchetto FRESCO dal POD ricevuto DOPO
    // lo stop: e' la prova che il POD e' vivo e ha gia' ricevuto lo stato 0.
    static unsigned long t_stop = 0;
    if(t_stop == 0) t_stop = millis();
    bool po = (millis()-ultimoPacchetto<5000) && (ultimoPacchetto!=0);
    if(!po){
      if(Lvgl_lock(-1)){lv_label_set_text(label_upload_status,"ATTESA POD...");Lvgl_unlock();}
    } else if(ultimoPacchetto <= t_stop || millis()-t_stop < 1000UL){
      // Nessun pacchetto ancora arrivato dopo lo stop (o troppo presto):
      // il POD non ha ancora confermato di essere in ascolto.
      if(Lvgl_lock(-1)){lv_label_set_text(label_upload_status,"INVIO AL POD...");Lvgl_unlock();}
    } else {
      pending_cloud_upload=false; t_stop=0;
      if(Lvgl_lock(-1)){lv_label_set_text(label_upload_status,datiNautici.cloud_connesso?"DATI CLOUD OK!":"SALVATO SU POD (WIFI OFF)");Lvgl_unlock();}
    }
  }
  // V45.5.19: inviaDatiAlPod() chiamato SEMPRE — non dipende da pending_cloud_upload.
  // FIX: il gate bloccava la TX ESP-NOW durante la sessione → POD sordo → LINK LOST.
  // pending_cloud_upload controlla SOLO il label UI (sotto), non la trasmissione radio.
  if(nuoviDatiRicevuti){nuoviDatiRicevuti=false;aggiornaUI();inviaDatiAlPod();}
  if(millis()-ultimoPacchetto>5000&&ultimoPacchetto!=0){
    if(Lvgl_lock(-1)){
      // V45.5.12: array bl[] a 9 elementi (aggiunta wpn)
      lv_obj_t *bl[]={lbl_bat_m,lbl_bat_maxi,lbl_bat_gw,lbl_bat_gwp,
                      lbl_bat_wpn,lbl_bat_ht,lbl_bat_s,lbl_bat_sum,lbl_bat_info};
      for(int i=0;i<9;i++) lv_label_set_text(bl[i],"LINK LOST");
      Lvgl_unlock();
    }
    ultimoPacchetto=0;
  }
  lv_tick_inc(5);delay(5);
}

void scansionaCanaliRadio(){
  // ── V45.5.17 — Riaggancio via scan SSID (sostituisce hopping cieco) ────
  // STORIA DEL BUG:
  //  - Il POD e' WIFI_AP_STA e l'ESP32 ha UNA sola radio: AP e STA
  //    condividono il canale. Con WiFi.begin(router) l'AP viene forzato sul
  //    canale del ROUTER, ignorando il 6 della softAP(). Se il router fa
  //    channel switching, il canale del POD CAMBIA nel tempo.
  //    Evidenza: ch=5 a un boot, ch=2 al successivo.
  //  - L'hopping cieco 1..13 ogni 600ms si riaggancia solo per fortuna, e
  //    dopo LINK LOST (ultimoPacchetto=0) non si fermava mai.
  //  - esp_wifi_get_channel() dentro OnDataRecv e' inaffidabile: la callback
  //    gira nel task WiFi e puo' essere schedulata dopo un cambio canale.
  // FIX: quando il link cade, ri-scansiona l'SSID del POD (AYE_POD_NET).
  // WiFi.scanNetworks() restituisce il canale ESATTO dell'AP: nessuna stima,
  // nessun hopping. E' la stessa strategia gia' usata da trovaPodSegugio()
  // al boot, qui resa ripetibile a runtime.
  const unsigned long RISCAN_OGNI_MS = 3000UL;   // non piu' di 1 scan / 3s

  // Link vivo: nulla da fare.
  if(millis()-ultimoPacchetto<2000 && ultimoPacchetto!=0){
    if(!canaleTrovato) canaleTrovato=true;
    return;
  }

  // NB: dopo LINK LOST il loop azzera ultimoPacchetto (=0). Non usiamo piu'
  // quel flag come guardia: la ri-scansione parte comunque.
  canaleTrovato=false;

  // ── V45.6.1 — FIX "POD offline dopo lo stop dal Visore" ────────────────
  // BUG (16/07, osservato in campo): allo stop il POD spariva dalla web app
  // per 145-209s e si riprendeva solo staccando/riattaccando il WiFi.
  // Nei dati: ciclo telemetria regolarissimo a 2.8s PRIMA dello stop, buco
  // netto, poi di nuovo 2.9s. Un POD in crisi avrebbe cicli irregolari: qui
  // e' la RADIO che cade, non il POD.
  //
  // CATENA:
  //  1. stop_session() -> session_active=false
  //  2. il POD nello stesso ciclo fa POST telemetria + RPC chiudi = 2 TLS
  //     -> resta occupato ~3-5s e non trasmette ESP-NOW
  //  3. il Visore non riceve per >2s -> cade la guardia "link vivo"
  //  4. ma session_active e' gia' false -> la protezione V45.5.20 NON vale piu'
  //  5. WiFi.scanNetworks() parte: blocca la radio ~1.5s e cambia canale
  //  6. il POD torna libero ma il Visore e' altrove -> rescan ogni 3s -> loop
  //
  // FIX: la protezione non finisce con la sessione. Resta attiva per
  // SCAN_GRACE_MS dopo lo stop — il tempo che il POD chiuda la sessione a DB
  // e torni a trasmettere. In questa finestra il Visore NON scansiona: si
  // limita a riposizionarsi sul canale noto (operazione non bloccante).
  // 20s copre abbondantemente 2 handshake TLS + retry (osservato max ~5s).
  //
  // t_fine_sessione e' armato in stop_session(); 0 = nessuna sessione appena
  // chiusa. La finestra e' one-shot: scaduta, il comportamento torna quello
  // normale (scan pieno) per non compromettere il riaggancio dopo LINK LOST.
  const unsigned long SCAN_GRACE_MS = 20000UL;
  bool graceAttiva = (t_fine_sessione != 0) &&
                     ((millis() - t_fine_sessione) < SCAN_GRACE_MS);
  if(t_fine_sessione != 0 && !graceAttiva) t_fine_sessione = 0;  // finestra chiusa

  // V45.5.20: durante sessione attiva NON fare scan bloccante.
  // Il POD è sul canale noto (canalePodNoto) — basta riposizionarsi lì.
  // WiFi.scanNetworks() blocca ~1.5s e interrompe ESP-NOW, causando
  // un loop scan→no link→scan quando il POD è occupato in HTTPS.
  if((session_active || graceAttiva) && canalePodNoto > 0){
    if(canaleAttuale != canalePodNoto){
      canaleAttuale = canalePodNoto;
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_channel(canaleAttuale, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);
      Serial.printf("[RADIO] %s — ritorno su canale noto %d (nessuno scan)\n",
                    session_active ? "Sessione attiva" : "POD occupato in chiusura", canalePodNoto);
    }
    return;  // nessun scan, aspetta che il POD torni
  }

  if(millis()-tempoUltimoCambioCanale < RISCAN_OGNI_MS) return;
  tempoUltimoCambioCanale=millis();

  // Scan bloccante (~1.5s): accettabile solo quando link perso e NON in sessione.
  int n = WiFi.scanNetworks(false, false);
  int ch = -1;
  for(int i=0;i<n;i++){
    if(WiFi.SSID(i)==target_ssid){ ch=WiFi.channel(i); break; }
  }
  WiFi.scanDelete();

  if(ch>0){
    if(ch!=canaleAttuale){
      canaleAttuale=ch;
      esp_wifi_set_promiscuous(true);esp_wifi_set_channel(canaleAttuale,WIFI_SECOND_CHAN_NONE);esp_wifi_set_promiscuous(false);
      Serial.printf("[RADIO] POD trovato su canale %d — riaggancio\n", ch);
    }
    canalePodNoto=ch;
  } else {
    Serial.println("[RADIO] AP del POD non visibile — POD spento o fuori portata");
  }
}
