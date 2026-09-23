#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"
#include "synthB_AnalogEnv.h"

// =========================================================================
// synthB_Engine.h — Motore audio del SynthB
// =========================================================================
// Contiene:
//   - ADSR ausiliario (NON applicato al DCO per ora)
//   - Note stack mono
//   - Allocazione slot poly
//   - Freq/glide per-slot
//   - Wavetable setup (unica, globale)
//   - NoteOn / NoteOff / AllNotesOff
//   - IRQ PWM (on_pwm_wrap)
// =========================================================================

// -------------------------------------------------------------------------
// 1. ADSR AUSILIARIO (accettato via LWS, non applicato al DCO)
// -------------------------------------------------------------------------
// Predisposto per revisione futura dell'architettura envelope.
// Attualmente il DCO usa gate secco (noteOn/noteOff).
// -------------------------------------------------------------------------
enum AuxEnvState : uint8_t {
    AUX_ENV_IDLE = 0,
    AUX_ENV_ATTACK,
    AUX_ENV_DECAY,
    AUX_ENV_SUSTAIN,
    AUX_ENV_RELEASE
};

static uint8_t auxEnvState    = AUX_ENV_IDLE;
static float   auxEnvPhase    = 0.0f;
static float   auxEnvLevel    = 0.0f;
static float   auxEnvRelStart = 0.0f;

static inline float auxAdsrTimeSeconds(uint8_t p) {
    if (p == 0) return 0.0f;
    float norm = (float)p / 255.0f;
    return norm * norm * 3.0f;   // 0..3 s
}

static inline void auxAdsr_noteOn() {
    auxEnvState = AUX_ENV_ATTACK;
    auxEnvPhase = 0.0f;
}

static inline void auxAdsr_noteOff() {
    if (auxEnvState == AUX_ENV_IDLE) return;
    auxEnvState    = AUX_ENV_RELEASE;
    auxEnvPhase    = 0.0f;
    auxEnvRelStart = auxEnvLevel;
}

static inline void auxAdsr_tick() {
    const float dt = 1.0f / PWM_IRQ_RATE_HZ;
    switch (auxEnvState) {
        case AUX_ENV_IDLE:
            auxEnvLevel = 0.0f;
            break;
        case AUX_ENV_ATTACK: {
            float t = auxAdsrTimeSeconds(vir_adsr_a);
            if (t < 1e-4f) {
                auxEnvLevel = 1.0f;
                auxEnvState = AUX_ENV_DECAY;
                auxEnvPhase = 0.0f;
                break;
            }
            auxEnvPhase += dt / t;
            if (auxEnvPhase >= 1.0f) {
                auxEnvLevel = 1.0f;
                auxEnvState = AUX_ENV_DECAY;
                auxEnvPhase = 0.0f;
            } else {
                auxEnvLevel = auxEnvPhase;
            }
            break;
        }
        case AUX_ENV_DECAY: {
            float t    = auxAdsrTimeSeconds(vir_adsr_d);
            float sLev = (float)vir_adsr_s / 255.0f;
            if (t < 1e-4f) {
                auxEnvLevel = sLev;
                auxEnvState = AUX_ENV_SUSTAIN;
                break;
            }
            auxEnvPhase += dt / t;
            if (auxEnvPhase >= 1.0f) {
                auxEnvLevel = sLev;
                auxEnvState = AUX_ENV_SUSTAIN;
            } else {
                auxEnvLevel = 1.0f - auxEnvPhase * (1.0f - sLev);
            }
            break;
        }
        case AUX_ENV_SUSTAIN:
            auxEnvLevel = (float)vir_adsr_s / 255.0f;
            break;
        case AUX_ENV_RELEASE: {
            float t = auxAdsrTimeSeconds(vir_adsr_r);
            if (t < 1e-4f) {
                auxEnvLevel = 0.0f;
                auxEnvState = AUX_ENV_IDLE;
                break;
            }
            auxEnvPhase += dt / t;
            if (auxEnvPhase >= 1.0f) {
                auxEnvLevel = 0.0f;
                auxEnvState = AUX_ENV_IDLE;
            } else {
                auxEnvLevel = auxEnvRelStart * (1.0f - auxEnvPhase);
            }
            break;
        }
    }
}

// -------------------------------------------------------------------------
// 2. NOTE STACK (usato solo in mono)
// -------------------------------------------------------------------------
static void noteStackPush(uint8_t pitch) {
    // evita duplicati
    for (uint8_t i = 0; i < noteCount; i++)
        if (noteStack[i] == pitch) return;

    if (noteCount >= NOTE_STACK_MAX) {
        // overflow: scarta la più vecchia
        for (uint8_t i = 1; i < NOTE_STACK_MAX; i++)
            noteStack[i - 1] = noteStack[i];
        noteCount = NOTE_STACK_MAX - 1;
    }
    noteStack[noteCount++] = pitch;
    currentNote = pitch;
}

static bool noteStackRemove(uint8_t pitch) {
    int found = -1;
    for (uint8_t i = 0; i < noteCount; i++)
        if (noteStack[i] == pitch) { found = i; break; }
    if (found < 0) return false;

    for (uint8_t i = found; i < noteCount - 1; i++)
        noteStack[i] = noteStack[i + 1];
    noteCount--;

    if (noteCount > 0)
        currentNote = noteStack[noteCount - 1];
    return true;
}

// -------------------------------------------------------------------------
// 3. FREQUENZA E GLIDE
// -------------------------------------------------------------------------
// Calcola la frequenza base per un pitch MIDI (0..127).
static float calcFreq(uint8_t pitch) {
    int n = (int)pitch - 24;
    if (n < 0)                    n = 0;
    if (n > MAX_NOTEARRAY_IDX)    n = MAX_NOTEARRAY_IDX;
    int ot = (int)noteArr[n] + (int)calb;
    if (ot > 1225) ot = 1225;
    return 256.0f * freq_table[ot] / 122070.0f * (float)oct_sw;
}

// Aggiorna lo stato di glide per uno slot.
// Se slideTimeMs == 0 → salto diretto.
static void computeGlideStep(uint8_t slot) {
    if (slideTimeMs == 0) {
        glideStep[slot]    = 0.0f;
        glideCurrent[slot] = glideTarget[slot];
        glideActive[slot]  = 0;
        return;
    }
    float ticks = (float)slideTimeMs * (PWM_IRQ_RATE_HZ / 1000.0f);
    float diff  = fabsf(glideTarget[slot] - glideCurrent[slot]);
    glideStep[slot] = diff / ticks;
    if (glideStep[slot] < 1e-7f) glideStep[slot] = 1e-7f;
    glideActive[slot] = 1;
}

// Assegna il target a uno slot (con o senza glide).
// Se startFreq != 0 e diversa da target, applica glide; altrimenti salto.
static void assignFreqToSlot(uint8_t slot, float target, bool glide) {
    glideTarget[slot] = target;
    if (!glide || glideCurrent[slot] < 1e-6f || glideCurrent[slot] == target) {
        glideCurrent[slot] = target;
        osc_freq[slot]     = target;
        glideActive[slot]  = 0;
        glideStep[slot]    = 0.0f;
    } else {
        computeGlideStep(slot);
    }
}

// -------------------------------------------------------------------------
// 4. WAVETABLE SETUP (unica, globale)
// -------------------------------------------------------------------------
static void wavetable_setup() {
    if (mode == 0) {
        // ---------- WAVEFOLD ----------
        switch (waveform) {
            case 0: // SAW
                for (int i = 0; i < 256; i++) wavetable[i] = i * 4 - (int)sampleLev + 1;
                break;
            case 1: // SINE
                for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2 * i / 256.0f) * sampleLev);
                break;
            case 2: // SQR
                for (int i = 0; i < 128; i++) { wavetable[i] = (int)sampleLev; wavetable[i+128] = -(int)sampleLev; }
                break;
            case 3: // TRI
                for (int i = 0; i < 128; i++) {
                    wavetable[i]       = i * 8 - (int)sampleLev;
                    wavetable[i + 128] = (int)sampleLev - i * 8;
                }
                break;
            case 4: // OCT-SAW
                for (int i = 0; i < 128; i++) {
                    wavetable[i]       = i * 4 - ((int)sampleLev + 1) + i * 2;
                    wavetable[i + 128] = i * 2 - (((int)sampleLev + 1) / 2) + i * 4;
                }
                break;
            case 5: // FM1
                for (int i = 0; i < 256; i++)
                    wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*3.0f*i/256.0f)) * sampleLev);
                break;
            case 6: // FM2
                for (int i = 0; i < 256; i++)
                    wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*7.0f*i/256.0f)) * sampleLev);
                break;
            case 7: // FM3
                for (int i = 0; i < 256; i++)
                    wavetable[i] = (int)(sinf(PIx2*i/256.0f +
                                              sinf(PIx2*4.0f*i/256.0f +
                                                   sinf(PIx2*11.0f*i/256.0f))) * sampleLev);
                break;
            case 8: // NOISE
                for (int i = 0; i < 256; i++) wavetable[i] = random(511, 1020) - (int)sampleLev;
                break;
            default:
                for (int i = 0; i < 256; i++) wavetable[i] = 0;
                break;
        }
        attenua = (waveform == 2) ? 6 : 5;
    }
    else if (mode == 2) {
        // ---------- AM ----------
        switch (waveform) {
            case 0: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f) * sampleLev); break;
            case 1: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*3.0f*i/256.0f)) * sampleLev); break;
            case 2: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*5.0f*i/256.0f)) * sampleLev); break;
            case 3: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*4.0f*i/256.0f + sinf(PIx2*11.0f*i/256.0f))) * sampleLev); break;
            case 4: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*1.28f*i/256.0f)) * sampleLev); break;
            case 5: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*3.19f*i/256.0f)) * sampleLev); break;
            case 6: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*2.3f*i/256.0f + sinf(PIx2*7.3f*i/256.0f))) * sampleLev); break;
            case 7: for (int i = 0; i < 256; i++) wavetable[i] = (int)(sinf(PIx2*i/256.0f + sinf(PIx2*6.3f*i/256.0f + sinf(PIx2*11.3f*i/256.0f))) * sampleLev); break;
            case 8: for (int i = 0; i < 256; i++) wavetable[i] = random(511, 1020) - (int)sampleLev + 1; break;
            default: for (int i = 0; i < 256; i++) wavetable[i] = 0; break;
        }
        attenua = 5;
    }
    else {
        // ---------- FM ---------- (mod2_wavetable riempita da loop1)
        for (int i = 0; i < 256; i++) wavetable[i] = 0;
        attenua = 5;
    }

    // Copia diretta in mod2 (aggiornata poi da loop1)
    for (int i = 0; i < 256; i++) mod2_wavetable[i] = wavetable[i];
}

// -------------------------------------------------------------------------
// 5. ALLOCAZIONE SLOT (poly)
// -------------------------------------------------------------------------
// Traccia l'età di ogni nota per determinare la vittima in caso di steal.
static uint32_t slot_last_note[POLIMAX] = {};
static uint32_t note_counter = 0;

// Ritorna il primo slot libero, o -1 se non ce ne sono.
static int findFreeSlot() {
    for (int i = 0; i < POLIMAX; i++) {
        if (noteOnArr[i] == 0) return i;
    }
    return -1;
}

// Ritorna lo slot più "vecchio" (nota triggerata meno di recente).
static int findOldestSlot() {
    int oldest = 0;
    uint32_t minT = slot_last_note[0];
    for (int i = 1; i < POLIMAX; i++) {
        if (slot_last_note[i] < minT) {
            minT = slot_last_note[i];
            oldest = i;
        }
    }
    return oldest;
}

// -------------------------------------------------------------------------
// 6. NOTE HANDLING — MONO
// -------------------------------------------------------------------------
static void monoNoteOn(uint8_t pitch, uint8_t vel) {
    UNUSED(vel);

    noteStackPush(pitch);
    float newFreq = calcFreq(currentNote);

    // Se lo slot 0 era silenzioso (freq ~0), salta; altrimenti glide.
    bool wasSilent = (osc_freq[0] < 1e-6f);

    if (wasSilent) {
        // Fresh start: salto, reset fase
        glideCurrent[0] = newFreq;
        osc_freq[0]     = newFreq;
        glideTarget[0]  = newFreq;
        glideStep[0]    = 0.0f;
        glideActive[0]  = 0;
        f[0]            = 0.0f;
    } else {
        // Legato / last-note priority: glide
        glideTarget[0] = newFreq;
        computeGlideStep(0);
    }
    noteOnArr[0]     = currentNote;
    noteOnFreqArr[0] = currentNote;

    // Nota: auxAdsr è aggiornato ma NON applicato al DCO
    auxAdsr_noteOn();
}

static void monoNoteOff(uint8_t pitch) {
    if (!noteStackRemove(pitch)) return;

    if (noteCount == 0) {
        // Tutte le note rilasciate: silenzio.
        // Reset freq per far "ripartire da zero" la prossima nota.
        noteOnArr[0] = 0;
        osc_freq[0]  = 0.0f;
        auxAdsr_noteOff();
    } else {
        // Torna alla nota precedente con glide
        float newFreq = calcFreq(currentNote);
        glideTarget[0] = newFreq;
        computeGlideStep(0);
        noteOnArr[0]     = currentNote;
        noteOnFreqArr[0] = currentNote;
    }
}

// -------------------------------------------------------------------------
// 7. NOTE HANDLING — POLY
// -------------------------------------------------------------------------
static void polyNoteOn(uint8_t pitch, uint8_t vel) {
    UNUSED(vel);

    int slot = findFreeSlot();
    bool stolen = false;

    if (slot < 0) {
        slot = findOldestSlot();
        stolen = true;
    }

    float newFreq = calcFreq(pitch);

    if (stolen) {
        // Steal: glide dal freq corrente al nuovo
        assignFreqToSlot((uint8_t)slot, newFreq, true);
    } else {
        // Fresh: salto, reset fase
        f[slot]             = 0.0f;
        glideCurrent[slot]  = newFreq;
        osc_freq[slot]      = newFreq;
        glideTarget[slot]   = newFreq;
        glideStep[slot]     = 0.0f;
        glideActive[slot]   = 0;
    }

    noteOnArr[slot]     = pitch;
    noteOnFreqArr[slot] = pitch;
    slot_last_note[slot] = ++note_counter;

    // auxAdsr: aggiornato solo per completezza, non applicato
    auxAdsr_noteOn();
}

static void polyNoteOff(uint8_t pitch) {
    for (int i = 0; i < POLIMAX; i++) {
        if (noteOnArr[i] == pitch) {
            noteOnArr[i] = 0;
            // Non resettiamo freq/glide: saranno sovrascritti al prossimo NoteOn
            // (fresh slot = jump, stolen = glide)
            break;
        }
    }
    // auxAdsr solo se non ci sono più note attive
    bool anyActive = false;
    for (int i = 0; i < POLIMAX; i++) if (noteOnArr[i] != 0) { anyActive = true; break; }
    if (!anyActive) auxAdsr_noteOff();
}

// -------------------------------------------------------------------------
// 8. DISPATCH NOTE ON/OFF (mono vs poly)
// -------------------------------------------------------------------------
static void onNoteOn(uint8_t pitch, uint8_t velocity) {
    bool wasSilent;
    if (synthBMode == 0) {
        wasSilent = (noteCount == 0);
        monoNoteOn(pitch, velocity);
    } else {
        bool anyActive = false;
        for (int i = 0; i < POLIMAX; i++)
            if (noteOnArr[i] != 0) { anyActive = true; break; }
        wasSilent = !anyActive;
        polyNoteOn(pitch, velocity);
    }
    if (wasSilent) analogEnvNoteOn();
}

static void onNoteOff(uint8_t pitch) {
    if (synthBMode == 0) {
        monoNoteOff(pitch);
        // In mono: noteCount diventa 0 → release
        if (noteCount == 0) analogEnvNoteOff();
    } else {
        polyNoteOff(pitch);
        // In poly: controlla se ci sono ancora note attive
        bool anyActive = false;
        for (int i = 0; i < POLIMAX; i++)
            if (noteOnArr[i] != 0) { anyActive = true; break; }
        if (!anyActive) analogEnvNoteOff();
    }
}

static void onAllNotesOff() {
    for (int i = 0; i < POLIMAX; i++) {
        noteOnArr[i] = 0;
        osc_freq[i]  = 0.0f;
        glideActive[i] = 0;
        glideStep[i]   = 0.0f;
    }
    noteCount   = 0;
    currentNote = 0;
    auxAdsr_noteOff();
}

// -------------------------------------------------------------------------
// 9. IRQ PWM WRAP
// -------------------------------------------------------------------------
// Somma i contributi di tutti gli slot attivi. Applica clamp.
// Gli slot "silenti" (noteOnArr==0) non avanzano fase e non contribuiscono.
// -------------------------------------------------------------------------
void on_pwm_wrap() {
    pwm_clear_irq(slice_num);

    int32_t levelOut = 0;

    for (int i = 0; i < POLIMAX; i++) {
        // --- Glide update ---
        if (glideActive[i]) {
            if (glideCurrent[i] < glideTarget[i]) {
                glideCurrent[i] += glideStep[i];
                if (glideCurrent[i] >= glideTarget[i]) {
                    glideCurrent[i] = glideTarget[i];
                    glideActive[i]  = 0;
                }
            } else if (glideCurrent[i] > glideTarget[i]) {
                glideCurrent[i] -= glideStep[i];
                if (glideCurrent[i] <= glideTarget[i]) {
                    glideCurrent[i] = glideTarget[i];
                    glideActive[i]  = 0;
                }
            }
            osc_freq[i] = glideCurrent[i];
        }

        // --- Solo se lo slot è attivo ---
        if (noteOnArr[i] == 0) continue;

        // --- Avanza fase ---
        f[i] += osc_freq[i];
        while (f[i] >= 256.0f) f[i] -= 256.0f;
        while (f[i] < 0.0f)    f[i] += 256.0f;

        // --- Leggi campione ---
        int32_t samp = mod2_wavetable[(uint8_t)f[i]];
        levelOut += samp;
    }

    // --- AUX ADSR tick (non applicato al DCO, solo per stato) ---
    auxAdsr_tick();

    // --- Normalizzazione (POLIMAX voci possono sommare) ---
    levelOut = levelOut / POLIMAX;

    // --- Clamp al range PWM (±512 con centro 512) ---
    if (levelOut >  512) levelOut =  512;
    if (levelOut < -512) levelOut = -512;

    pwm_set_chan_level(slice_num, PWM_CHAN_B, (uint16_t)(levelOut + 512));
}