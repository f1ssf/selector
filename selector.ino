// SELECTOR.INO. avant slider   //
// F1SSF - 02/01/2026.Pilotage relais + IHM Nextion (Arduino Mega 2560) //
// SELECTEUR D'ANTENNE MULTIFONCTION  //
// SOIT POUR PILOTER UN RELAIS TRANSCO JUSQU'A 8 VOIES - TEMPORISE //
// SOIT POUR PILOTER EN MANUEL 8 RELAIS  //
// SOIT POUR PILOTER PAR RADIO IC7300 et IC9100 PAR ENTREE A0 //
// HMI avec NEXTION  NX4832T035_011  //
// Documentation // 
// Relais actif HIGH avec module Amazon //
// Boot : relais 1..8 OFF + boutons gris (OFF) //
// Page 0 (manuel) : bm1..bm8 -> ON (interlock global, un seul relais), bm0 STOP //
// Page 1 (temporisée) : bt1..bt8 -> ON 2s puis OFF (non bloquant), interlock global //
// Page 3 (ICOM) : br9100 / br7300 sélection modèle radio (identiques pour l'instant) //
// A0 (8 niveaux) commande le relais (interlock global) + filtrage 0.2v ajustable dans le code //
// Niveaux relais 1..8 : 0V, 1.5V, 2V, 2.5V, 3V, 3.5V, 4V, 4.5V  tolérance ±0.2V //
// Nextion config : release event only : prints "<bm1>",0 + CRLF ; send component ID OFF //
// ------------------------------------------------------------------//

#include <Arduino.h>

// ========================= //
//      Configuration        //
// ========================= //

#define DEBUG_SERIAL 1
#define NEXTION_SERIAL Serial3
static const uint32_t NEXTION_BAUD = 9600;

// Relais
static const uint8_t RELAY_COUNT = 8;
static const uint8_t RELAY_PINS[RELAY_COUNT] = {22,23,24,25,26,27,28,29};
static const uint8_t RELAY_ON_LEVEL  = HIGH;
static const uint8_t RELAY_OFF_LEVEL = LOW;

// Tempo relais (mode TIMED) – réglable via slider Nextion
static uint32_t timedPulseMs = 2000;

// A0 ICOM
static const uint8_t  ICOM_AIN_PIN = A0;
static const float    ADC_VREF = 5.0f;
static const uint16_t ADC_MAX  = 1023;

// Filtrage analogique
static const float    V_TOL = 0.2f;               // tolérance ±0.2V
static const uint32_t A0_SAMPLE_PERIOD_MS = 50;
static const uint32_t A0_STABLE_TIME_MS   = 150;

// Sécurité relais
static const uint8_t RELAY_SWITCH_DEADTIME_MS = 5;

// Couleurs Nextion
static const uint16_t NEX_COLOR_OFF = 50712;
static const uint16_t NEX_COLOR_ON  = 63488;

// Anti-rebond commandes série Nextion
static const uint32_t CMD_DEGLITCH_MS = 40;

// ========================= //
//           États           //
// ========================= //

enum class Mode       : uint8_t { MANUAL, TIMED, ICOM };
enum class RadioModel : uint8_t { NONE, IC_9100, IC_7300 };

static Mode       currentMode  = Mode::MANUAL;
static RadioModel currentRadio = RadioModel::NONE;

static uint8_t  activeRelay = 0;
static bool     timedRunning = false;
static uint32_t timedStartMs = 0;

// Boot sync Nextion
static bool     bootColorSynced = false;
static uint32_t bootSyncStartMs = 0;

// RX Nextion
static char     rxLine[32];
static uint8_t  rxLen = 0;
static char     lastCmd[32] = {0};
static uint32_t lastCmdMs = 0;

// A0 state
static uint32_t lastA0SampleMs = 0;
static uint8_t  lastA0Candidate = 0;
static uint32_t a0CandidateSinceMs = 0;
static uint8_t  a0ValidatedRelay = 0;

// ========================= //
//      Nextion TX           //
// ========================= //

static void nexSendRaw(const char* cmd){
  NEXTION_SERIAL.print(cmd);
  NEXTION_SERIAL.write(0xFF); NEXTION_SERIAL.write(0xFF); NEXTION_SERIAL.write(0xFF);
}

static void nexSetBcoBco2(const char* obj, uint16_t c){
  char buf[64];
  snprintf(buf,sizeof(buf),"%s.bco=%u",obj,c);  nexSendRaw(buf);
  snprintf(buf,sizeof(buf),"%s.bco2=%u",obj,c); nexSendRaw(buf);
}

static void nexSetRelayButtonManual(uint8_t r,bool on){
  char n[8]; snprintf(n,sizeof(n),"bm%u",r);
  nexSetBcoBco2(n,on?NEX_COLOR_ON:NEX_COLOR_OFF);
}

static void nexSetRelayButtonTimed(uint8_t r,bool on){
  char n[8]; snprintf(n,sizeof(n),"bt%u",r);
  nexSetBcoBco2(n,on?NEX_COLOR_ON:NEX_COLOR_OFF);
}

static void nexSetRadioButtons(){
  nexSetBcoBco2("br9100",(currentRadio==RadioModel::IC_9100)?NEX_COLOR_ON:NEX_COLOR_OFF);
  nexSetBcoBco2("br7300",(currentRadio==RadioModel::IC_7300)?NEX_COLOR_ON:NEX_COLOR_OFF);
}

static void nexUpdateVisualOnRelayChange(uint8_t o,uint8_t n){
  if(currentMode==Mode::MANUAL){
    if(o) nexSetRelayButtonManual(o,false);
    if(n) nexSetRelayButtonManual(n,true);
  }
  if(currentMode==Mode::TIMED){
    if(o) nexSetRelayButtonTimed(o,false);
    if(n) nexSetRelayButtonTimed(n,true);
  }
  if(currentMode==Mode::ICOM){
    nexSetRadioButtons();
  }
}

static void nexFullSync(){
  for(uint8_t i=1;i<=8;i++){
    nexSetRelayButtonManual(i,(currentMode==Mode::MANUAL && activeRelay==i));
    nexSetRelayButtonTimed (i,(currentMode==Mode::TIMED  && activeRelay==i));
  }
  nexSetRadioButtons();
}

// ========================= //
//           Relais           //
// ========================= //

static void relayAllOff(){
  for(uint8_t i=0;i<RELAY_COUNT;i++)
    digitalWrite(RELAY_PINS[i],RELAY_OFF_LEVEL);
}

static void relaySet(uint8_t i,bool on){
  if(i<1||i>8) return;
  digitalWrite(RELAY_PINS[i-1],on?RELAY_ON_LEVEL:RELAY_OFF_LEVEL);
}

static void activateRelay(uint8_t idx){
  if(idx==activeRelay) return;
  uint8_t old=activeRelay;
  relayAllOff();
  activeRelay=0;
  if(RELAY_SWITCH_DEADTIME_MS) delay(RELAY_SWITCH_DEADTIME_MS);
  if(idx){ relaySet(idx,true); activeRelay=idx; }
  nexUpdateVisualOnRelayChange(old,activeRelay);
}

static void stopAll(){
  timedRunning=false;
  a0ValidatedRelay=0;
  activateRelay(0);
}

// *********** WARNING ********** WARNING ******************** WARNING*****************//
// ***** ⚠️  UTILISER UN PONT DIVISEUR POUR LA TRANSPOSITION 0-8V vers 0 -5V ⚠️ ***** //
// *********************⚠️  SINON DESTRUCTION DE L'ARDUINO ⚠️  ********************** //
// *********** WARNING ********** WARNING ******************** WARNING*****************//

//   Vin radio
//   o
//   |
//   |
//   [ R1 ] =4.82K
//   |
//   +---------> VA0 (max 5 V)
//   |
//   [ R2 ] =10K
//   |
//   |
//   GND


// =============================//
//    Origine Icom France       //
//     BAND    VINradio   V-A0  //
//    1.8 MHZ   7.41V   5.00V   //
//    3.5 MHZ   6.06V   4.09V   //
//      7 MHZ   5.07V   3.42V   //
//     10 MHZ   0V       0V     //
//     14 MHZ   4.07V   2.75V   //
//     18 MHZ   3.19V   2.15V   //
//     21 MHZ   3.19V   2.15V   //
//     24 MHZ   2.23V   1.5V    //
//     28 MHZ   2.23V   1.5V    //
//     50 MHZ   1.89V   1.28V   //
// ============================ //

// ⚠️ À AJUSTER LES TENSIONS RÉELLES DU POSTE ⚠️ //
// Index 0 = relais 1, index 7 = relais 8 //
// Attention l'ordre des relais n'est pas lineaire //
// IC7300 tensions linéaire de demo //

static const float RELAY_VNOM_9100[8] = {
  5.00, 4.09, 3.42, 0, 2.75, 2.15, 1.5, 1.28
};

static const float RELAY_VNOM_7300[8] = {
  0.5, 1.0, 1.6, 2.0, 2.6, 3.6, 4.5, 5.0
};

static uint16_t v2a(float v){
  if(v<=0) return 0;
  if(v>=ADC_VREF) return ADC_MAX;
  return (uint16_t)((v/ADC_VREF)*ADC_MAX+0.5f);
}

static uint8_t decodeA0(uint16_t a){
  const float* table = nullptr;

  if(currentRadio == RadioModel::IC_9100) table = RELAY_VNOM_9100;
  else if(currentRadio == RadioModel::IC_7300) table = RELAY_VNOM_7300;
  else return 0; // sécurité : radio non sélectionnée

  if(a <= v2a(V_TOL)) return 1;

  for(uint8_t i=2;i<=8;i++){
    float v = table[i-1];
    if(a >= v2a(v - V_TOL) && a <= v2a(v + V_TOL))
      return i;
  }
  return 0;
}

static void processA0(){
  uint32_t now=millis();
  if(now-lastA0SampleMs<A0_SAMPLE_PERIOD_MS) return;
  lastA0SampleMs=now;

  uint8_t c=decodeA0(analogRead(ICOM_AIN_PIN));

  if(c!=lastA0Candidate){
    lastA0Candidate=c;
    a0CandidateSinceMs=now;
    return;
  }

  if(c==0){
    if(a0ValidatedRelay){
      a0ValidatedRelay=0;
      activateRelay(0);
    }
    return;
  }

  if(now-a0CandidateSinceMs>=A0_STABLE_TIME_MS && a0ValidatedRelay!=c){
    a0ValidatedRelay=c;
    activateRelay(c);
  }
}

// ========================= //
//      Nextion RX           //
// ========================= //

static bool isDup(const char* s){
  uint32_t now=millis();
  if(strcmp(s,lastCmd)==0 && now-lastCmdMs<CMD_DEGLITCH_MS) return true;
  strncpy(lastCmd,s,sizeof(lastCmd)-1);
  lastCmdMs=now;
  return false;
}

static void handleCmd(const char* s){
  if(!s||!s[0]) return;

#if DEBUG_SERIAL
  Serial.print("RX: ");
  Serial.println(s);
#endif

  if(isDup(s)) return;

  if(strncmp(s,"<T:",3)==0){
    uint8_t v = atoi(&s[3]);
    if(v>=1 && v<=10) timedPulseMs = (uint32_t)v * 1000UL;
    return;
  }

  if(strcmp(s,"<bm0>")==0){ stopAll(); return; }

  if(strncmp(s,"<bm",3)==0){
    currentMode=Mode::MANUAL;
    uint8_t i=atoi(&s[3]);
    activateRelay(activeRelay==i?0:i);
    return;
  }

  if(strncmp(s,"<bt",3)==0){
    currentMode=Mode::TIMED;
    timedRunning=true;
    timedStartMs=millis();
    activateRelay(atoi(&s[3]));
    return;
  }

  if(strcmp(s,"<br9100>")==0){ currentMode=Mode::ICOM; currentRadio=RadioModel::IC_9100; stopAll(); return; }
  if(strcmp(s,"<br7300>")==0){ currentMode=Mode::ICOM; currentRadio=RadioModel::IC_7300; stopAll(); return; }
}

static void pollNextion(){
  while(NEXTION_SERIAL.available()){
    char c=NEXTION_SERIAL.read();
    if(c=='\n'){
      rxLine[rxLen]=0;
      if(rxLen&&rxLine[rxLen-1]=='\r') rxLine[rxLen-1]=0;
      handleCmd(rxLine);
      rxLen=0;
    } else if(rxLen<sizeof(rxLine)-1 && c>=0x20){
      rxLine[rxLen++]=c;
    }
  }
}

// ========================= //
//           Setup            //
// ========================= //

void setup(){
#if DEBUG_SERIAL
  Serial.begin(115200);
  delay(500);
  Serial.println("DEBUG OK");
#endif

  NEXTION_SERIAL.begin(NEXTION_BAUD);

  for(uint8_t i=0;i<RELAY_COUNT;i++){
    pinMode(RELAY_PINS[i],OUTPUT);
    digitalWrite(RELAY_PINS[i],RELAY_OFF_LEVEL);
  }

  pinMode(ICOM_AIN_PIN,INPUT);
  bootSyncStartMs = millis();
}

// ========================= //
//            Loop            //
// ========================= //

void loop(){
  if(!bootColorSynced){
    if(millis()-bootSyncStartMs<1200) nexFullSync();
    else bootColorSynced=true;
  }

  pollNextion();

  if(timedRunning && millis()-timedStartMs>=timedPulseMs){
    timedRunning=false;
    activateRelay(0);
  }

  if(currentMode==Mode::ICOM && currentRadio!=RadioModel::NONE)
    processA0();
}
