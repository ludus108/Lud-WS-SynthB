#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"
#include "synthB_AnalogEnv.h"   // per ana_state, ana_sustain, ANA_*
#include "synthB_Engine.h"      // per auxEnvState, auxEnvLevel, AUX_ENV_*, vir_adsr_s
#include "synthB_Lfo3.h" 
// =========================================================================
// synthB_Vcf.h — Gestione dei 3 VCF (modi Filter e Wovel)
// =========================================================================
// FILTER mode, sottomodi:
//   0 = Unisono : vcf2 = vcf3 = vcf1 (cut2/cut3 ignorati)
//   1 = Slave   : vcf2 = vcf1 + (cut2 - 128), vcf3 = vcf1 + (cut3 - 128)
//   2 = Free    : ogni VCF indipendente
//
// WOVEL mode, sottomodi:
//   0 = pot_aeiou  : pot 0..255 percorre 5 transizioni A→E→I→O→U→A
//   1 = env_aeiou  : vocale A (start) → B (sustain) → C (release)
//                    guidato da ADSR digitale (vir) o hardware (ana)
//   2 = random     : su NoteOn sceglie vocale random, transizione temporizzata
//
// Applica la modulazione LFO3 (lfo3Mod[]) e scrive sul TLC5628.
//
// NB: tlcWrite() e TLC_CH_* sono definite inline nel .ino prima di questo
//     header. lfo3Mod[] è definita in synthB_Lfo3.h.
// =========================================================================


// -------------------------------------------------------------------------
// Array formanti — frequenze di taglio per vocali italiane A E I O U
// (indici 0..4 = A, E, I, O, U; indici 5..9 ripetono per compatibilità
//  con l'originale e per eventuali estensioni future)
// -------------------------------------------------------------------------
static const uint8_t nodeF1[10] = {129,107, 86,127,108,129,127,119,110, 98};
static const uint8_t nodeF2[10] = {146,159,165,133,131,146,159,161,163,147};
static const uint8_t nodeF3[10] = {167,169,177,175,172,167,170,172,173,177};


// -------------------------------------------------------------------------
// Costanti sottomodi
// -------------------------------------------------------------------------
#define WOV_SUB_POT_AEIOU   0
#define WOV_SUB_ENV_AEIOU   1
#define WOV_SUB_RANDOM      2

#define WOV_ENV_SRC_VIR     0    // morph guidato da ADSR digitale (vir_*)
#define WOV_ENV_SRC_ANA     1    // morph guidato da ADSR hardware (ana_*)


// -------------------------------------------------------------------------
// Stato — Filter / Wovel
// -------------------------------------------------------------------------
static uint8_t vcf1Cut = 128;   // 0..255
static uint8_t vcf2Cut = 128;   // 0..255 (offset bidirezionale in Slave)
static uint8_t vcf3Cut = 128;   // 0..255

static uint8_t vcfMode    = 0;  // 0=Filter, 1=Wovel
static uint8_t vcfSubMode = 2;  // per Filter: 0=Uni, 1=Slave, 2=Free

// --- Wovel: parametri comuni ---
static uint8_t  wovVowelA  = 0;    // 0..255 (Display)
static uint8_t  wovVowelB  = 255;  // 0..255
static uint8_t  wovMorph   = 0;    // 0..255 (usato in pot mode)
static int32_t  wovFormant = 250;  // 0..500

// --- Wovel: submode ---
static uint8_t  wovSubMode = WOV_SUB_POT_AEIOU;

// --- Wovel: env_aeiou ---
static uint8_t  wovEnvSrc     = WOV_ENV_SRC_VIR;  // 0=vir, 1=ana
static uint8_t  wovEnvVowelA  = 0;   // 0..4 (start)
static uint8_t  wovEnvVowelB  = 2;   // 0..4 (sustain)
static uint8_t  wovEnvVowelC  = 4;   // 0..4 (release)
static uint8_t  wovEnvAttack  = 50;  // 0..255 (informativo)

// --- Wovel: random ---
static uint8_t  wovRndStart  = 0;    // 0..4
static uint8_t  wovRndTarget = 0;    // 0..4
static uint8_t  wovRndMorph  = 255;  // 0..255 (255 = arrivato)
static uint32_t wovRndT0     = 0;
static int32_t  wovRndTimeMs = 200;  // 0..5000


// -------------------------------------------------------------------------
// Mappa 0..255 → 0..4 (indice vocale)
// -------------------------------------------------------------------------
static inline uint8_t vowelIdx(uint8_t v) {
    if (v > 254) return 4;
    return (uint8_t)(((uint16_t)v * 5) >> 8);   // v/256 * 5
}


// -------------------------------------------------------------------------
// Morph lineare tra due vocali per un array formanti
// -------------------------------------------------------------------------
static inline int vowelMorph(const uint8_t* arr, uint8_t iA, uint8_t iB, uint8_t m) {
    int a = arr[iA];
    int b = arr[iB];
    return a + ((b - a) * (int)m) / 255;
}


// -------------------------------------------------------------------------
// Wovel env: calcola morph 0..255 basato su ADSR digitale O hardware
// -------------------------------------------------------------------------
// Convenzione: 0 = vocale A, 128 = vocale B, 255 = vocale C
// - ATTACK  : morph 0 → 128
// - DECAY   : morph = 128 (B)
// - SUSTAIN : morph = 128 (B)
// - RELEASE : morph 128 → 255
// - IDLE    : morph = 0 (A)
//
// wovEnvSrc = 0: usa ADSR DIGITALE (auxEnvLevel, auxEnvState)
// wovEnvSrc = 1: usa ADSR HARDWARE (analogRead PIN_ADC_ENV + ana_state)
// -------------------------------------------------------------------------
static inline int wovEnvMorphNow() {

    if (wovEnvSrc == WOV_ENV_SRC_VIR) {
        // =========================================================
        // Sorgente: ADSR DIGITALE (vir_*)
        // =========================================================
        int lev = (int)(auxEnvLevel * 255.0f);   // 0..255

        switch (auxEnvState) {
            case AUX_ENV_ATTACK:
                return map(lev, 0, 255, 0, 128);

            case AUX_ENV_DECAY:
            case AUX_ENV_SUSTAIN:
                return 128;

            case AUX_ENV_RELEASE: {
                int sLev = (int)vir_adsr_s;
                if (sLev < 20) {
                    return map(lev, 0, 255, 255, 128);
                }
                int m = map(lev, 0, sLev, 255, 128);
                return constrain(m, 128, 255);
            }

            case AUX_ENV_IDLE:
            default:
                return 0;
        }
    } else {
        // =========================================================
        // Sorgente: ADSR HARDWARE (ana_*)
        // =========================================================
        int envADC = analogRead(PIN_ADC_ENV);

        switch (ana_state) {
            case ANA_ATTACK:
                return map(envADC, 0, 1000, 0, 128);

            case ANA_DECAY:
            case ANA_SUSTAIN:
                return 128;

            case ANA_RELEASE: {
                if (ana_sustain < 20) {
                    return map(envADC, 0, 1000, 255, 128);
                }
                int m = map(envADC, 0, ana_sustain, 255, 128);
                return constrain(m, 128, 255);
            }

            case ANA_IDLE:
            default:
                return 0;
        }
    }
}


// -------------------------------------------------------------------------
// Wovel random: da chiamare ad ogni NoteOn "da silenzio"
// -------------------------------------------------------------------------
static void wovRandomOnNoteOn() {
    if (vcfMode != 1) return;                  // non in wovel
    if (wovSubMode != WOV_SUB_RANDOM) return;  // non in random

    wovRndStart = wovRndTarget;

    uint8_t newV = (uint8_t)random(0, 5);
    if (newV == wovRndStart) newV = (newV + 1) % 5;
    wovRndTarget = newV;

    wovRndT0    = millis();
    wovRndMorph = 0;
}


// -------------------------------------------------------------------------
// Wovel random: aggiorna morph in base al tempo (chiamato da loop())
// -------------------------------------------------------------------------
static inline void wovRandomUpdate() {
    if (vcfMode != 1) return;
    if (wovSubMode != WOV_SUB_RANDOM) return;
    if (wovRndMorph >= 255) return;

    if (wovRndTimeMs <= 0) {
        wovRndMorph = 255;
        wovRndStart = wovRndTarget;
        return;
    }

    uint32_t elapsed = millis() - wovRndT0;
    if (elapsed >= (uint32_t)wovRndTimeMs) {
        wovRndMorph = 255;
        wovRndStart = wovRndTarget;
    } else {
        wovRndMorph = (uint8_t)((elapsed * 255) / (uint32_t)wovRndTimeMs);
    }
}


// -------------------------------------------------------------------------
// Calcola i 3 cut VCF in base a mode/submode
// -------------------------------------------------------------------------
static void computeVcfBase(int out[3]) {

    if (vcfMode == 0) {
        // ---------- FILTER ----------
        switch (vcfSubMode) {
            case 0:  // Unisono: vcf2 = vcf3 = vcf1
                out[0] = vcf1Cut;
                out[1] = vcf1Cut;
                out[2] = vcf1Cut;
                break;

            case 1:  // Slave: vcf2/vcf3 = vcf1 + offset bidirezionale
                out[0] = vcf1Cut;
                out[1] = constrain((int)vcf1Cut + ((int)vcf2Cut - 128), 0, 255);
                out[2] = constrain((int)vcf1Cut + ((int)vcf3Cut - 128), 0, 255);
                break;

            case 2:  // Free: indipendenti
            default:
                out[0] = vcf1Cut;
                out[1] = vcf2Cut;
                out[2] = vcf3Cut;
                break;
        }
    } else {
        // ---------- WOVEL ----------
        uint8_t vowelA = 0;
        uint8_t vowelB = 0;
        uint8_t morph  = 0;

        switch (wovSubMode) {

            case WOV_SUB_POT_AEIOU: {
                // Pot 0..255 → 5 transizioni (A→E→I→O→U→A)
                uint8_t pos = wovMorph;
                uint8_t pairIdx;
                uint8_t localMorph;

                if (pos < 51) {
                    pairIdx    = 0;
                    localMorph = map(pos,   0,  50, 0, 255);
                } else if (pos < 102) {
                    pairIdx    = 1;
                    localMorph = map(pos,  51, 101, 0, 255);
                } else if (pos < 153) {
                    pairIdx    = 2;
                    localMorph = map(pos, 102, 152, 0, 255);
                } else if (pos < 204) {
                    pairIdx    = 3;
                    localMorph = map(pos, 153, 203, 0, 255);
                } else {
                    pairIdx    = 4;
                    localMorph = map(pos, 204, 255, 0, 255);
                }

                static const uint8_t seq[5] = {0, 1, 2, 3, 4};   // A E I O U
                vowelA = seq[pairIdx];
                vowelB = seq[(pairIdx + 1) % 5];
                morph  = localMorph;
                break;
            }

            case WOV_SUB_ENV_AEIOU: {
                int m = wovEnvMorphNow();   // 0..255

                if (m < 128) {
                    vowelA = wovEnvVowelA;
                    vowelB = wovEnvVowelB;
                    morph  = (uint8_t)(m * 2);              // 0..254
                } else {
                    vowelA = wovEnvVowelB;
                    vowelB = wovEnvVowelC;
                    morph  = (uint8_t)((m - 128) * 2);      // 0..254
                }
                break;
            }

            case WOV_SUB_RANDOM:
            default:
                vowelA = wovRndStart;
                vowelB = wovRndTarget;
                morph  = wovRndMorph;
                break;
        }

        // Calcola morph per ogni array formanti
        int v1 = vowelMorph(nodeF1, vowelA, vowelB, morph);
        int v2 = vowelMorph(nodeF2, vowelA, vowelB, morph);
        int v3 = vowelMorph(nodeF3, vowelA, vowelB, morph);

        // Offset formant: 0..500 → circa -22..+61
        int formantOff = (int)(wovFormant / 6) - 22;

        out[0] = constrain(v1 + formantOff, 0, 255);
        out[1] = constrain(v2 + formantOff, 0, 255);
        out[2] = constrain(v3 + formantOff, 0, 255);
    }
}


// -------------------------------------------------------------------------
// TICK — calcola output, applica LFO3, scrive sul DAC
// -------------------------------------------------------------------------
static void vcfTick() {
    int base[3];
    computeVcfBase(base);

    // Applica modulazione LFO3 (bipolare -255..+255)
    int o1 = base[0] + lfo3Mod[0];
    int o2 = base[1] + lfo3Mod[1];
    int o3 = base[2] + lfo3Mod[2];

    uint8_t out1 = (uint8_t)constrain(o1, 0, 255);
    uint8_t out2 = (uint8_t)constrain(o2, 0, 255);
    uint8_t out3 = (uint8_t)constrain(o3, 0, 255);

    // Scritture TLC5628
    tlcWrite(TLC_CH_VCF1, out1);
    tlcWrite(TLC_CH_VCF2, out2);
    tlcWrite(TLC_CH_VCF3, out3);
}


// -------------------------------------------------------------------------
// Refresh immediato (usato al cambio cut/mode)
// -------------------------------------------------------------------------
static inline void vcfRefresh() {
    vcfTick();
}