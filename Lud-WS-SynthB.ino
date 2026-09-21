// =====================================================================
// Lud-WS-SynthB — RP2040 synth parafonico a 6 slot, nodo LWS
// =====================================================================
// REV 1 (LWS integration)
//
// ARCHITETTURA
// ---------------------------------------------------------------------
//   Router ── SerialSynthB (SerialPIO GP8/9, 115200) ──▶ [B] SynthB
//
// Il SynthB è parafonico: uno strumento, 6 slot interni (POLIMAX=6).
// Non ha voci separate: i comandi LWS usano sempre voice=0.
//
// MODI:
//   mono  (synthBMode=0): 1 sola nota alla volta, glide tra note consecutive.
//   poly  (synthBMode=1): fino a 6 note contemporanee, allocazione dinamica.
//                          Anche in poly lo slide è attivo (ogni slot glida).
//
// DCO: gate secco (nessun ADSR applicato).
// ADSR ausiliario: parametri accettati via LWS ma non applicati al DCO.
//
// MIDI: hardware gestito dal Router. Le note arrivano come CMD_MIDI_*_V.
//
// =====================================================================

#include <Arduino.h>
#include <hardware/pwm.h>
#include <EEPROM.h>

#include "synthB_Config.h"
#include "ottaveB.h"           // tabelle numeriche (ripulito)

#include "serial_protocol.h"
#include "comunicazioni_mcu.h"

#include "synthB_State.h"

// ---------------------------------------------------------------------
// Definizione delle variabili dichiarate in synthB_State.h
// ---------------------------------------------------------------------

// --- Tabelle FM ---
float fmSetSin[8][3] = {};
int   fmSetDiv[8][3] = {};

// --- Tabelle UI ---
const int attenuaNumArr[10] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

// --- Curva volume ---
const float levArr[21] = {
    0.0f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f,
    0.35f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f, 0.65f,
    0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f
};

// --- Costanti globali ---
const float masterFreq    = 4.0f;
const float f0            = 30.0f;
float       calb          = 6.58f;
const float sampleLev     = 551.0f;
float       freq_table[2048];
int         sinePitchModArr[256];
int         sineModArr[256];
float       maxModPitchLev = 300.0f;

// --- PWM ---
int slice_num = 0;

// --- Parametri globali ---
uint8_t mode     = 0;
uint8_t waveform = 0;
uint8_t fmSel    = 0;
uint8_t ottava   = 2;
uint8_t oct_sw   = 2;
uint8_t attenua  = 5;

// --- LFO ---
float          mod         = 0.0f;
int            modIn       = 0;
int            modInB      = 0;
int            modLev      = 0;
int            modLevA     = 0;
unsigned long  speedMod    = 12000UL;
unsigned long  prevTimeMod = 0;
int            contaMod    = 0;

int            modPitchLev     = 0;
int            modPitchLevA    = 0;
unsigned long  speedPitchMod   = 500UL;
unsigned long  prevTimePitchMod = 0;
int            contaPitchMod   = 0;

// --- Bend ---
float   frBend        = 0.0f;
float   frMod         = 0.0f;
uint8_t bendMaxUp     = 0;
uint8_t bendMaxDown   = 2;

// --- Slide ---
uint32_t slideTimeMs = 200;

// --- Modalità ---
uint8_t synthBMode = 0;   // 0=mono, 1=poly

// --- ADSR ausiliario ---
uint8_t aux_adsr_a = 10;
uint8_t aux_adsr_d = 30;
uint8_t aux_adsr_s = 200;
uint8_t aux_adsr_r = 40;

// --- Preset ---
uint8_t presetSel  = 0;
uint8_t presetSave = 0;

// --- Slot parafonici ---
float  f[POLIMAX]         = {};
float  osc_freq[POLIMAX]  = {};
uint8_t noteOnArr[POLIMAX]     = {};
int     noteOnFreqArr[POLIMAX] = {};

uint8_t glideActive[POLIMAX]  = {};
float   glideTarget[POLIMAX]  = {};
float   glideCurrent[POLIMAX] = {};
float   glideStep[POLIMAX]    = {};

// --- Mono note stack ---
uint8_t noteStack[NOTE_STACK_MAX] = {};
uint8_t noteCount   = 0;
uint8_t currentNote = 0;

// --- Wavetable ---
int   wavetable[256]     = {};
float mod_wavetable[256] = {};
int   mod2_wavetable[256] = {};

// --- AM ---
int      am_k     = 0;
uint32_t am_timer = 0;

// ---------------------------------------------------------------------
// Include moduli funzionali
// ---------------------------------------------------------------------
#include "synthB_Engine.h"
#include "synthB_Control.h"
#include "synthB_Lws.h"

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Lud-WS-SynthB ===");
    Serial.printf("POLIMAX=%u\n", POLIMAX);

    // --- LWS ---
    LWS_PORT.begin(LWS_BAUD);

    // --- Init tabelle ---
    for (int i = 0; i < 256; i++) {
        sinePitchModArr[i] = (int)(sinf(PIx2 * i / 256.0f) * 511.0f);
        sineModArr[i]      = (int)(sinf(PIx2 * i / 256.0f) * 511.0f);
    }
    for (int i = 0; i < 1230; i++)
        freq_table[i] = f0 * powf(2.0f, voctpow[i]);
    for (int i = 1230; i < 2048; i++) freq_table[i] = 6.0f;

    // --- Init stato ---
    mode     = 0;      // wavefold
    waveform = 0;      // SAW
    fmSel    = 0;
    ottava   = 2;
    oct_sw   = 2;
    attenua  = 5;

    for (int i = 0; i < POLIMAX; i++) {
        f[i]              = 0.0f;
        osc_freq[i]       = 0.0f;
        noteOnArr[i]      = 0;
        noteOnFreqArr[i]  = 0;
        glideActive[i]    = 0;
        glideTarget[i]    = 0.0f;
        glideCurrent[i]   = 0.0f;
        glideStep[i]      = 0.0f;
    }

    wavetable_setup();

    // --- PWM ---
    gpio_set_function(OUTPUT_A_PIN, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(OUTPUT_A_PIN);
    pwm_clear_irq(slice_num);
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_set_enabled(slice_num, true);

    pwm_set_clkdiv(slice_num, masterFreq);
    pwm_set_wrap(slice_num, 1023);

    Serial.println("SynthB ready.");
}

// ---------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------
void loop() {
    // --- LWS poll ---
    pollLws();

    // --- Calcolo "mod" (globale) ---
    int tmpmod = modIn + modInB;
    tmpmod = (tmpmod > 1023) ? 1023 : tmpmod;
    switch (mode) {
        case 0:
            if (waveform != 2) mod = (float)tmpmod * 0.0036f + 0.90f;
            else               mod = (float)(tmpmod >> 3);
            break;
        case 1: mod = (float)(tmpmod >> 3); break;
        case 2: mod = (float)(1023 - tmpmod); break;
    }

    // --- LFO tick ---
    lfoTick();
}