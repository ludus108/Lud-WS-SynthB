#pragma once
#include "synthB_Config.h"

// =========================================================================
// synthB_State.h — Stato globale del SynthB
// =========================================================================
// Tutte le variabili sono globali al chip (nessuna per-voce).
// Gli slot parafonici (POLIMAX) hanno solo parametri dinamici.
// =========================================================================

// -------------------------------------------------------------------------
// 1. TABELLE COSTANTI
// -------------------------------------------------------------------------
// Definite in ottaveB.h (invariato dal progetto originale).

extern const float voctpow[1230];
extern const float noteArr[61];
extern const int   bendMaxUpArr[5];
extern const int   bendMaxDownArr[5];

// -------------------------------------------------------------------------
// 2. COSTANTI GLOBALI MOTORE
// -------------------------------------------------------------------------
extern const float masterFreq;      // 4.0
extern const float f0;              // 30.0
extern float       calb;            // 6.58
extern const float sampleLev;       // 551
extern float       freq_table[2048];
extern int         sinePitchModArr[256];
extern int         sineModArr[256];
extern float       maxModPitchLev;  // 300.0

// -------------------------------------------------------------------------
// 3. TABELLE FM (globali chip)
// -------------------------------------------------------------------------
extern float fmSetSin[8][3];
extern int   fmSetDiv[8][3];

// -------------------------------------------------------------------------
// 4. TABELLE UI (usate solo per preset lookup)
// -------------------------------------------------------------------------
extern const int   attenuaNumArr[10];

// -------------------------------------------------------------------------
// 5. CURVA VOLUME
// -------------------------------------------------------------------------
extern const float levArr[21];

// -------------------------------------------------------------------------
// 6. STATO PWM
// -------------------------------------------------------------------------
extern int slice_num;

// -------------------------------------------------------------------------
// 7. STATO PARAMETRI GLOBALI CHIP
// -------------------------------------------------------------------------
extern uint8_t mode;          // 0=WF, 1=FM, 2=AM
extern uint8_t waveform;      // 0..8
extern uint8_t fmSel;         // 0..7 (indice fmSetSin/Div)
extern uint8_t ottava;        // 1..3
extern uint8_t oct_sw;        // 1, 2, 4 (derivato da ottava)
extern uint8_t attenua;       // 1..10 (indice attenuaNumArr)

// -------------------------------------------------------------------------
// 8. STATO LFO (globale)
// -------------------------------------------------------------------------
extern float          mod;           // "mod" del loop
extern int            modIn;
extern int            modInB;
extern int            modLev;
extern int            modLevA;
extern unsigned long  speedMod;
extern unsigned long  prevTimeMod;
extern int            contaMod;

extern int            modPitchLev;
extern int            modPitchLevA;
extern unsigned long  speedPitchMod;
extern unsigned long  prevTimePitchMod;
extern int            contaPitchMod;

// -------------------------------------------------------------------------
// 9. STATO BEND (globale)
// -------------------------------------------------------------------------
extern float   frBend;
extern float   frMod;
extern uint8_t bendMaxUp;
extern uint8_t bendMaxDown;

// -------------------------------------------------------------------------
// 10. STATO SLIDE (globale)
// -------------------------------------------------------------------------
extern uint32_t slideTimeMs;    // 0..1000 (tempo di glide)

// -------------------------------------------------------------------------
// 11. STATO MODALITÀ
// -------------------------------------------------------------------------
extern uint8_t synthBMode;      // 0=mono, 1=poly

// --- ADSR digitale ausiliario (vir) ---
extern uint8_t vir_adsr_a;
extern uint8_t vir_adsr_d;
extern uint8_t vir_adsr_s;
extern uint8_t vir_adsr_r;

// --- ADSR hardware (ana) ---
extern uint8_t ana_attack;    // 0..7 (canale 4051)
extern uint8_t ana_decay;     // 0..7
extern uint16_t ana_sustain;   // 0..1023 (livello ADC target)
extern uint8_t ana_release;   // 0..7
extern uint8_t ana_env_mode;  // 0=ADSR, 1=AD

// -------------------------------------------------------------------------
// 13. STATO PRESET
// -------------------------------------------------------------------------
extern uint8_t presetSel;       // 0..29
extern uint8_t presetSave;      // 0..29

// -------------------------------------------------------------------------
// 14. STATO SLOT PARAFONICI (POLIMAX)
// -------------------------------------------------------------------------
// Oscillatori (fase + frequenza)
extern float  f[POLIMAX];              // accumulatore fase 0..255
extern float  osc_freq[POLIMAX];       // incremento per IRQ

// Note attive (0 = slot libero, altrimenti pitch MIDI della nota)
extern uint8_t noteOnArr[POLIMAX];      // pitch della nota (0 = libero)
extern int     noteOnFreqArr[POLIMAX];  // per riferimento

// Glide per-slot (lo slide è esteso a tutte le voci)
extern uint8_t glideActive[POLIMAX];    // 0/1
extern float   glideTarget[POLIMAX];
extern float   glideCurrent[POLIMAX];
extern float   glideStep[POLIMAX];

// -------------------------------------------------------------------------
// 15. MONO NOTE STACK (usato in synthBMode == 0)
// -------------------------------------------------------------------------
extern uint8_t noteStack[NOTE_STACK_MAX];
extern uint8_t noteCount;
extern uint8_t currentNote;

// -------------------------------------------------------------------------
// 16. STATO WAVETABLE (una sola condivisa)
// -------------------------------------------------------------------------
extern int   wavetable[256];
extern float mod_wavetable[256];
extern int   mod2_wavetable[256];

// -------------------------------------------------------------------------
// 17. STATO AM (mode 2)
// -------------------------------------------------------------------------
extern int      am_k;
extern uint32_t am_timer;