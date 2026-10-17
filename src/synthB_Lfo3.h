#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"

// =========================================================================
// synthB_Lfo3.h — Terzo LFO con 3 counter indipendenti e spread
// =========================================================================
// Genera 8 forme d'onda e le invia ai 3 VCF tramite il TLC5628.
// I 3 counter condividono la forma ma sono sfasati da `lfo3Spread`:
//   spread = 0    → tutti in fase (0°)
//   spread = 255  → counter 0 a 0°, counter 1 a 90°, counter 2 a 180°
//
// NB: tlcWrite() e TLC_CH_* sono definite inline nel .ino, prima di
//     questo header.
// =========================================================================

// --- Forme d'onda ---
#define LFO3_SINE      0
#define LFO3_SAW       1
#define LFO3_RSAW      2
#define LFO3_SQR       3
#define LFO3_RND       4
#define LFO3_RND50     5
#define LFO3_RND25     6
#define LFO3_RND10     7

// --- Stato LFO3 ---
static uint8_t  lfo3Wave      = LFO3_SINE;
static uint8_t  lfo3Spread    = 0;         // 0..255
static int32_t  lfo3Rate      = 12000;     // µs (int32)
static int32_t  lfo3Lev       = 0;         // 0..1023 (depth)
static uint32_t lfo3LastTick  = 0;
// --- Output bipolare LFO3 (-255..+255), usato dal modulo VCF ---
static int16_t lfo3Mod[3] = {0, 0, 0};

static uint8_t  lfo3BasePhase = 0;               // counter comune 0..255
static int      lfo3RndHold[3] = {128, 128, 128};// hold random per counter

// --- Tabelle forme d'onda (popolate in initLfo3) ---
static int lfo3SineArr[256];
static int lfo3SawArr[256];
static int lfo3RsawArr[256];

// --- Output verso VCF (0..255) ---
static uint8_t  vcf1Out = 128;
static uint8_t  vcf2Out = 128;
static uint8_t  vcf3Out = 128;

// --- Valori base dei cutoff (impostati da K_VCF*_CUT) ---
static uint8_t  vcf1Base = 128;
static uint8_t  vcf2Base = 128;
static uint8_t  vcf3Base = 128;

// -------------------------------------------------------------------------
// INIT
// -------------------------------------------------------------------------
static void initLfo3() {
    for (int i = 0; i < 256; i++) {
        lfo3SineArr[i] = (int)(sinf(PIx2 * i / 256.0f) * 511.0f);
        lfo3SawArr[i]  = ((i * 1022) / 255) - 511;
        lfo3RsawArr[i] = 511 - ((i * 1022) / 255);
    }
    lfo3LastTick  = micros();
    lfo3BasePhase = 0;
    lfo3RndHold[0] = lfo3RndHold[1] = lfo3RndHold[2] = 128;

    LWS_DEBUG.println("[B] LFO3 init OK");
}

// -------------------------------------------------------------------------
// SAMPLE — ritorna -511..+511 per la forma selezionata
// -------------------------------------------------------------------------
static int lfo3Sample(uint8_t phase, uint8_t c) {
    switch (lfo3Wave) {
        case LFO3_SINE:  return lfo3SineArr[phase];
        case LFO3_SAW:   return lfo3SawArr[phase];
        case LFO3_RSAW:  return lfo3RsawArr[phase];
        case LFO3_SQR:   return (phase < 128) ? 511 : -511;

        // RND*: 3 valori indipendenti
        case LFO3_RND:
        case LFO3_RND50:
        case LFO3_RND25:
        case LFO3_RND10:
            return lfo3RndHold[c] - 128;

        default: return 0;
    }
}

// -------------------------------------------------------------------------
// Aggiornamento random (solo per forme RND*)
// -------------------------------------------------------------------------
static inline void lfo3UpdateRnd(uint8_t c, uint8_t percent) {
    if ((uint8_t)random(100) < percent) {
        lfo3RndHold[c] = (int)random(256);
    }
}

// -------------------------------------------------------------------------
// TICK — chiamato da loop() ad ogni iterazione
// -------------------------------------------------------------------------
static void lfo3Tick() {
    uint32_t now = micros();
    if ((now - lfo3LastTick) < (uint32_t)lfo3Rate) return;
    lfo3LastTick = now;

    // Avanza il counter base
    lfo3BasePhase++;
    if (lfo3BasePhase > 255) lfo3BasePhase = 0;

    // Fasi sfasate (opzione A: c1 a metà spread, c2 a spread completo)
    uint8_t phase0 =  lfo3BasePhase;
    uint8_t phase1 = (uint8_t)(lfo3BasePhase + (lfo3Spread >> 1));
    uint8_t phase2 = (uint8_t)(lfo3BasePhase + lfo3Spread);

    // Aggiornamento hold random indipendenti al rispettivo wrap
    if (lfo3Wave >= LFO3_RND) {
        uint8_t pct = 0;
        switch (lfo3Wave) {
            case LFO3_RND:   pct = 100; break;
            case LFO3_RND50: pct =  50; break;
            case LFO3_RND25: pct =  25; break;
            case LFO3_RND10: pct =  10; break;
        }
        if (phase0 == 0) lfo3UpdateRnd(0, pct);
        if (phase1 == 0) lfo3UpdateRnd(1, pct);
        if (phase2 == 0) lfo3UpdateRnd(2, pct);
    }

    // Leggi i 3 campioni
    int s0 = lfo3Sample(phase0, 0);
    int s1 = lfo3Sample(phase1, 1);
    int s2 = lfo3Sample(phase2, 2);

    // Applica depth (0..1023 → scala lineare)
    int mod0 = (s0 * lfo3Lev) / 1023;
    int mod1 = (s1 * lfo3Lev) / 1023;
    int mod2 = (s2 * lfo3Lev) / 1023;

    // Somma al base (mod/2 per adattare ±511 a ±255)
    int r0 = (int)vcf1Base + mod0 / 2;
    int r1 = (int)vcf2Base + mod1 / 2;
    int r2 = (int)vcf3Base + mod2 / 2;

       // Applica depth e salva in lfo3Mod (bipolare ±255)
    lfo3Mod[0] = (int16_t)((s0 * lfo3Lev) / 2048);
    lfo3Mod[1] = (int16_t)((s1 * lfo3Lev) / 2048);
    lfo3Mod[2] = (int16_t)((s2 * lfo3Lev) / 2048);
}

// -------------------------------------------------------------------------
// Dump per debug
// -------------------------------------------------------------------------
static inline void lfo3Dump() {
    LWS_DEBUG.printf("[B] LFO3 wave=%u spread=%u rate=%ld lev=%ld "
                     "out=%u,%u,%u\n",
                     lfo3Wave, lfo3Spread,
                     (long)lfo3Rate, (long)lfo3Lev,
                     vcf1Out, vcf2Out, vcf3Out);
}