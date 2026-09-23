#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"
#include "synthB_Engine.h"
// =========================================================================
// synthB_Control.h — Modulazioni (LFO mod + LFO pitch)
// =========================================================================
// SynthB non ha arp/gater né VCF LFO: solo i due LFO classici.
// Entrambi globali al chip (parafonia).
// =========================================================================

static void lfoTick() {
    unsigned long now = micros();

    // ---------------------------------------------------------------------
    // LFO mod (timbro)
    // ---------------------------------------------------------------------
    if ((now - prevTimeMod) > speedMod && modLev > 1) {
        contaMod++;
        if (contaMod > 255) contaMod = 0;

        int modVal = sineModArr[contaMod];
        float modA = (float)map(modVal, -511, 511, modLevA, modLev);

        modIn = (modLev > 1) ? (int)modA : 0;
        prevTimeMod = now;
    }

    // ---------------------------------------------------------------------
    // LFO pitch (vibrato)
    // ---------------------------------------------------------------------
    if ((now - prevTimePitchMod) > speedPitchMod && modPitchLev > 2) {
        contaPitchMod++;
        if (contaPitchMod > 255) contaPitchMod = 0;

        int modPVal = sinePitchModArr[contaPitchMod];
        float modP = (float)map(modPVal, -511, 511,
                                modPitchLevA, modPitchLev)
                     / maxModPitchLev;

        frMod = modP;
        prevTimePitchMod = now;
    }
}

// Applicazione bend + mod pitch alle frequenze degli slot.
// In SynthB (single PWM, freq per-slot) l'effetto bend+mod viene applicato
// come fattore moltiplicativo alla freq base di ciascuno slot attivo.
// Chiamata dal loop() ad ogni iterazione (basso costo).
static void applyPitchModulation() {
    if (frBend == 0.0f && frMod == 0.0f) return;

    float bendAmt = frBend * 2.0f + frMod;
    float factor  = powf(2.0f, bendAmt / 12.0f);

    // In mono: aggiorna lo slot 0
    // In poly: la freq di ogni slot è già corretta — va ricalcolata la freq
    //          base moltiplicata per factor. Il modo più pulito è aggiornare
    //          osc_freq in base al pitch della nota attiva sullo slot.
    if (synthBMode == 0) {
        if (noteOnArr[0] != 0) {
            float baseFreq = calcFreq(currentNote);
            osc_freq[0] = baseFreq * factor;
            // durante il glide, la freq è controllata da glideCurrent;
            // l'applicazione del bend in questo caso va fatta dentro l'IRQ.
            // Per ora, se il glide è attivo, saltiamo l'applicazione (il bend
            // verrà applicato al termine del glide).
            if (glideActive[0]) {
                osc_freq[0] = glideCurrent[0];
            }
        }
    } else {
        for (int i = 0; i < POLIMAX; i++) {
            if (noteOnArr[i] == 0) continue;
            float baseFreq = calcFreq(noteOnArr[i]);
            if (glideActive[i]) {
                osc_freq[i] = glideCurrent[i];
            } else {
                osc_freq[i] = baseFreq * factor;
            }
        }
    }
}