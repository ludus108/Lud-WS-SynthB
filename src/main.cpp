// =====================================================================
// Lud-WS-SynthB — RP2350  synth parafonico a 6 slot, nodo LWS
// =====================================================================
// Ver, 0.1.2
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

// ---------------------------------------------------------------------
// 1. Include base
// ---------------------------------------------------------------------
#include <Arduino.h>
#include <hardware/pwm.h>
#include <EEPROM.h>

#include "synthB_Config.h"
#include "ottaveB.h"

#include "serial_protocol.h"
#include "comunicazioni_mcu.h"

// ---------------------------------------------------------------------
// 2. DRIVER TLC5628 (inline, definito qui per essere visibile ai moduli)
// ---------------------------------------------------------------------
// DAC octal 8-bit, interfaccia seriale a 3 fili (DATA / CLK / LOAD).
// Parola 11 bit MSB-first: [A2 A1 A0 D7..D0]
// ---------------------------------------------------------------------

// Canali DAC
#define TLC_CH_VCF1     0    // OUTA → cutoff VCF1
#define TLC_CH_VCF2     1    // OUTB → cutoff VCF2
#define TLC_CH_VCF3     2    // OUTC → cutoff VCF3
#define TLC_CH_RES      3    // OUTD → resonance
#define TLC_CH_SUSTAIN  4    // OUTE → sustain voltage
// OUTF, OUTG, OUTH → riservati

// Scrittura singolo canale
static inline void tlcWrite(uint8_t channel, uint8_t value) {
    channel &= 0x07;
    uint16_t word = ((uint16_t)channel << 8) | (uint16_t)value;

    digitalWrite(PIN_TLC_LOAD, LOW);
    for (int i = 10; i >= 0; i--) {
        digitalWrite(PIN_TLC_CLK, LOW);
        digitalWrite(PIN_TLC_DATA, (word >> i) & 1);
        digitalWrite(PIN_TLC_CLK, HIGH);
    }
    digitalWrite(PIN_TLC_LOAD, HIGH);
    delayMicroseconds(1);
}

// Init
static inline void initTlc5628() {
    pinMode(PIN_TLC_DATA, OUTPUT);
    pinMode(PIN_TLC_CLK,  OUTPUT);
    pinMode(PIN_TLC_LOAD, OUTPUT);

    digitalWrite(PIN_TLC_DATA, LOW);
    digitalWrite(PIN_TLC_CLK,  LOW);
    digitalWrite(PIN_TLC_LOAD, HIGH);

    // Reset: tutti i canali a metà scala
    for (uint8_t ch = 0; ch < 8; ch++) tlcWrite(ch, 128);

    LWS_DEBUG.println("[B] TLC5628 init OK");
}

// ---------------------------------------------------------------------
// 3. Moduli funzionali (usano tlcWrite, perciò inclusi dopo il driver)
// ---------------------------------------------------------------------
#include "synthB_State.h"
#include "synthB_Engine.h"
#include "synthB_Control.h"
#include "synthB_AnalogEnv.h"
#include "synthB_Lfo3.h"
#include "synthB_Vcf.h"
#include "synthB_Lws.h"
// ---------------------------------------------------------------------
// Definizione delle variabili dichiarate in synthB_State.h
// ---------------------------------------------------------------------
uint8_t filterType = 0;
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
const float masterFreq    = PWM_CLKDIV_BASE;
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

// --- ADSR digitale ausiliario (vir) ---
uint8_t vir_adsr_a = 10;
uint8_t vir_adsr_d = 30;
uint8_t vir_adsr_s = 200;
uint8_t vir_adsr_r = 40;

// --- ADSR hardware (ana) ---
uint8_t ana_attack    = 0;      // ch 4051
uint8_t ana_decay     = 0;
uint16_t ana_sustain   = 512;    // 0..1023
uint8_t ana_release   = 0;
uint8_t ana_env_mode  = 0;      // 0=ADSR

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
// ===== STATISTICHE PER dumpStatus() =====
uint32_t g_frameFromRouter = 0;
uint32_t g_frameFromChain  = 0;
uint32_t g_pongSent        = 0;
uint32_t g_presetApplied   = 0;

// Forward declaration
void dumpStatus();
void dumpStatus() {
    // ===== HEADER =====
    Serial.println();
    Serial.println("[DUMP] ========== SYNTHB STATUS ==========");
    Serial.printf("[DUMP] mcu=%c uptime=%lu ms\n", MCU_ID, millis());
    Serial.printf("[DUMP] polimax=%d\n", POLIMAX);

    // ===== TRAFFIC =====
    Serial.println("[DUMP] --- TRAFFIC ---");
    Serial.printf("[DUMP] frame_from_router=%lu\n", g_frameFromRouter);
    Serial.printf("[DUMP] frame_from_chain=%lu\n",  g_frameFromChain);
    Serial.printf("[DUMP] pong_sent=%lu\n",         g_pongSent);
    Serial.printf("[DUMP] preset_applied=%lu\n",    g_presetApplied);

    // ===== MODE / SELECTIONS =====
    Serial.println("[DUMP] --- MODE ---");
    Serial.printf("[DUMP] mode=%u waveform=%u fmSel=%u\n",
                  mode, waveform, fmSel);
    Serial.printf("[DUMP] ottava=%u oct_sw=%u attenua=%u synthBMode=%u\n",
                  ottava, oct_sw, attenua, synthBMode);
    Serial.printf("[DUMP] filterType=%u presetSel=%u presetSave=%u\n",
                  filterType, presetSel, presetSave);
    Serial.printf("[DUMP] calb=%.2f masterFreq=%.2f f0=%.2f\n",
                  calb, masterFreq, f0);

    // ===== ENVELOPES =====
    Serial.println("[DUMP] --- ENVELOPES ---");
    Serial.printf("[DUMP] vir_adsr A=%u D=%u S=%u R=%u\n",
                  vir_adsr_a, vir_adsr_d, vir_adsr_s, vir_adsr_r);
    Serial.printf("[DUMP] ana_attack=%u ana_decay=%u ana_sustain=%u ana_release=%u\n",
                  ana_attack, ana_decay, ana_sustain, ana_release);
    Serial.printf("[DUMP] ana_env_mode=%u slideTimeMs=%lu\n",
                  ana_env_mode, slideTimeMs);

    // ===== MODULATION =====
    Serial.println("[DUMP] --- MODULATION ---");
    Serial.printf("[DUMP] modIn=%d modInB=%d modLev=%d modLevA=%d\n",
                  modIn, modInB, modLev, modLevA);
    Serial.printf("[DUMP] modPitchLev=%d modPitchLevA=%d\n",
                  modPitchLev, modPitchLevA);
    Serial.printf("[DUMP] speedMod=%lu speedPitchMod=%lu\n",
                  speedMod, speedPitchMod);
    Serial.printf("[DUMP] frBend=%.3f frMod=%.3f\n", frBend, frMod);
    Serial.printf("[DUMP] bendMaxUp=%u bendMaxDown=%u\n",
                  bendMaxUp, bendMaxDown);

    // ===== VOICES =====
    Serial.println("[DUMP] --- VOICES ---");
    for (int i = 0; i < POLIMAX; i++) {
        Serial.printf("[DUMP] slot%d note=%u freq=%d osc=%.2f glide=%u (cur=%.2f tgt=%.2f step=%.4f)\n",
                      i,
                      noteOnArr[i],
                      noteOnFreqArr[i],
                      osc_freq[i],
                      glideActive[i],
                      glideCurrent[i],
                      glideTarget[i],
                      glideStep[i]);
    }

    // ===== NOTE STACK / CURRENT =====
    Serial.printf("[DUMP] noteCount=%u currentNote=%u stack=[",
                  noteCount, currentNote);
    for (int i = 0; i < noteCount && i < NOTE_STACK_MAX; i++) {
        Serial.printf("%u", noteStack[i]);
        if (i < noteCount - 1) Serial.print(",");
    }
    Serial.println("]");

    // ===== FM OPERATORS =====
    Serial.println("[DUMP] --- FM OPERATORS ---");
    for (int p = 0; p < 8; p++) {
        Serial.printf("[DUMP] fm[%d] sin=%.2f,%.2f,%.2f div=%d,%d,%d\n",
                      p,
                      fmSetSin[p][0], fmSetSin[p][1], fmSetSin[p][2],
                      fmSetDiv[p][0], fmSetDiv[p][1], fmSetDiv[p][2]);
    }

    // ===== LFO3 =====
    Serial.println("[DUMP] --- LFO3 ---");
    Serial.printf("[DUMP] lfo3Wave=%u spread=%u rate=%ld lev=%ld\n",
                  lfo3Wave, lfo3Spread, (long)lfo3Rate, (long)lfo3Lev);
    Serial.printf("[DUMP] lfo3Mod=[%d,%d,%d] basePhase=%u\n",
                  lfo3Mod[0], lfo3Mod[1], lfo3Mod[2], lfo3BasePhase);

    // ===== VCF / WOVEL =====
    Serial.println("[DUMP] --- VCF / WOVEL ---");
    Serial.printf("[DUMP] vcfCut F1=%u F2=%u F3=%u\n",
                  vcf1Cut, vcf2Cut, vcf3Cut);
    Serial.printf("[DUMP] vcfMode=%u vcfSubMode=%u\n",
                  vcfMode, vcfSubMode);
    Serial.printf("[DUMP] wovVowelA=%u wovVowelB=%u wovMorph=%u wovSubMode=%u\n",
                  wovVowelA, wovVowelB, wovMorph, wovSubMode);
    Serial.printf("[DUMP] wovFormant=%ld wovEnvSrc=%u wovEnvAttack=%u\n",
                  (long)wovFormant, wovEnvSrc, wovEnvAttack);
    Serial.printf("[DUMP] wovEnvVowel A=%u B=%u C=%u\n",
                  wovEnvVowelA, wovEnvVowelB, wovEnvVowelC);

    // ===== PRESET =====
    Serial.println("[DUMP] --- PRESET ---");
    Serial.printf("[DUMP] presetActive=%d voice=%u id=%u len=%u rcvd=%u\n",
                  presetActive, presetVoice, presetId, presetLen, presetRcvd);

    // ===== FOOTER =====
    Serial.println("[DUMP] ==========================================");
    Serial.println();
}
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
	
	initAnalogEnv();
	initTlc5628();
	initLfo3();

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
	   if (Serial.available()) {
        char c = Serial.read();
        if (c == '?') dumpStatus();
    }
    // --- LWS poll ---
    pollLws();
	lfo3Tick();
analogEnvUpdate();
applyPitchModulation();
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
// =========================================================================
// CORE 1 — Rigenerazione wavetable modulata (mode 0=WF, 1=FM, 2=AM)
// =========================================================================
// Il core 1 gira in background e aggiorna continuamente mod2_wavetable[]
// in base a mode, waveform e mod (calcolati dal core 0).
// =========================================================================

volatile bool g_wt_busy = false;   // true = wavetable in riscrittura

void setup1() {
    // niente da inizializzare
}

void loop1() {
    while (true) {

        // ---------- WAVEFOLD ----------
        if (mode == 0) {
            g_wt_busy = true;
            if (waveform == 2) {
                // SQR: PWM
                int modInt = (int)mod;
                if (modInt < 0)   modInt = 0;
                if (modInt > 250) modInt = 250;
                for (int i = 0; i < 128 + modInt; i++)
                    mod2_wavetable[i] = 511;
                for (int i = 128 + modInt; i < 256; i++)
                    mod2_wavetable[i] = -511;
            } else {
                for (int i = 0; i < 256; i++)
                    mod_wavetable[i] = wavetable[i] * mod;
                for (int i = 0; i < 256; i++) {
                    float m = mod_wavetable[i];
                    if      (m >  511 && m <  1535) mod2_wavetable[i] =  1024 - (int)m;
                    else if (m < -512 && m > -1536) mod2_wavetable[i] = -1023 - (int)m;
                    else if (m < -1535)             mod2_wavetable[i] =  2048 + (int)m;
                    else if (m >  1534)             mod2_wavetable[i] =  (int)m - 2047;
                    else                            mod2_wavetable[i] =  (int)m;
                }
            }
            g_wt_busy = false;
        }

        // ---------- FM ----------
        else if (mode == 1) {
            g_wt_busy = true;
            float mm = mod;
            for (int i = 0; i < 256; i++) {
                float out = 0.0f;
                switch (waveform) {
                    case 0:
                        out = sinf(PIx2*i/256
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2])))) * 511;
                        break;
                    case 1:
                        out = (sinf(PIx2*i/256 + mm/128*sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]))
                             + sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                             + mm/128*sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2]))) * 250;
                        break;
                    case 2:
                        out = sinf(PIx2*i/256
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2])))) * 511;
                        break;
                    case 3:
                        out = sinf(PIx2*i/256
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2])))) * 511;
                        break;
                    case 4:
                        out = (sinf(PIx2*i/256 + mm/128*sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]))
                             + sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                             + mm/128*sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2]))) * 250;
                        break;
                    case 5:
                        out = (sinf(PIx2*i/256 + mm/128*sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]))
                             + sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                             + mm/128*sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2]))) * 250;
                        break;
                    case 6:
                        out = sinf(PIx2*i/256
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2])))) * 511;
                        break;
                    case 7:
                        out = sinf(PIx2*i/256
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][0]*i/fmSetDiv[fmSel][0]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][1]*i/fmSetDiv[fmSel][1]
                            + mm/128 * sinf(PIx2*fmSetSin[fmSel][2]*i/fmSetDiv[fmSel][2])))) * 511;
                        break;
                    default:
                        out = sinf(PIx2*i/256) * 511;
                        break;
                }
                mod2_wavetable[i] = (int)out;
            }
            g_wt_busy = false;
        }

        // ---------- AM ----------
        else if (mode == 2) {
            uint32_t now = micros();
            if ((now - am_timer) >= (uint32_t)mod) {
                g_wt_busy = true;
                am_k = (am_k < 63) ? am_k + 1 : 0;
                float sinVal = sinf(PIx2 * am_k / 63.0f);
                for (int i = 0; i < 256; i++)
                    mod2_wavetable[i] = (int)(wavetable[i] * sinVal);
                am_timer = now;
                g_wt_busy = false;
            }
        }

        delayMicroseconds(200);   // pausa per non saturare core 1
    }
}