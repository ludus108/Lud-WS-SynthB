#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"
#include "synthB_Engine.h"
#include "synthB_Control.h"

// =========================================================================
// synthB_Lws.h — Layer LWS del SynthB
// =========================================================================
// SynthB è nodo singolo:
//   - Una sola porta LWS
//   - Nessun bridge
//   - Nessun broadcast Poly
//   - I CMD MIDI *_V arrivano con voice sempre 0 (ignorata)
// =========================================================================

// -------------------------------------------------------------------------
// 1. CALLBACK APPLICATIVI — CMD_PARAM (uint8)
// -------------------------------------------------------------------------
static inline void on_param(char target, char key, uint8_t value) {
    if (target != MCU_ID) return;
    LWS_DEBUG.printf("[B] PARAM key=%c val=%u\n", key, value);

    switch (key) {
        // ---------- MODE / WAVEFORM ----------
        case K_MODE:
            mode = value;
            wavetable_setup();
            break;
        case K_WAVEFORM:
            waveform = value;
            wavetable_setup();
            break;
        case K_FM_SELECT:
            fmSel = value;
            break;

        // ---------- TUNING ----------
        case K_OTTAVA: {
            ottava = value;
            switch (value) {
                case 1:  oct_sw = 1; break;
                case 2:  oct_sw = 2; break;
                case 3:  oct_sw = 4; break;
                default: oct_sw = 2; break;
            }
            break;
        }
        case K_ATTENUA:
            attenua = value;
            break;

        // ---------- BENDER ----------
        case K_BEND_UP:   bendMaxUp   = value; break;
        case K_BEND_DOWN: bendMaxDown = value; break;

        // ---------- ADSR ausiliario (accettato, non applicato) ----------
        case K_ADSR_A: aux_adsr_a = value; break;
        case K_ADSR_D: aux_adsr_d = value; break;
        case K_ADSR_S: aux_adsr_s = value; break;
        case K_ADSR_R: aux_adsr_r = value; break;

        // ---------- MODALITÀ ----------
        case K_SYNTHB_MODE:
            synthBMode = (value != 0) ? 1 : 0;
            onAllNotesOff();   // reset pulito al cambio modo
            LWS_DEBUG.printf("[B] mode=%s\n", synthBMode ? "poly" : "mono");
            break;

        // ---------- SISTEMA ----------
        case K_PANIC:
            onAllNotesOff();
            break;
        case K_PRESET_SEL:  presetSel  = value; break;
        case K_PRESET_SAVE: presetSave = value; break;

        default:
            LWS_DEBUG.printf("[B] key sys non gestita: %c\n", key);
            break;
    }
}

// -------------------------------------------------------------------------
// 2. CALLBACK APPLICATIVI — CMD_PARAM_I32 (int32)
// -------------------------------------------------------------------------
static inline void on_param_i32(char target, char key, int32_t value) {
    if (target != MCU_ID) return;
    LWS_DEBUG.printf("[B] PARAM_I32 key=%c val=%ld\n", key, (long)value);

    switch (key) {
        // ---------- LFO mod ----------
        case K_MOD_IN_B: modInB = (int)value; break;
        case K_MOD_LEV:
            modLev  = (int)value;
            modLevA = modLev - (modLev * 2);
            break;
        case K_LFO_RATE:
            speedMod = (unsigned long)value;
            break;

        // ---------- LFO pitch ----------
        case K_PITCH_LEV:
            modPitchLev  = (int)value;
            modPitchLevA = modPitchLev - (modPitchLev * 2);
            break;
        case K_PITCH_RATE:
            speedPitchMod = (unsigned long)value;
            break;

        // ---------- SLIDE ----------
        case K_SLIDE_TIME: {
            uint32_t t = (uint32_t)value;
            if (t > 1000) t = 1000;
            slideTimeMs = t;
            // Se il glide è in corso, ricalcola lo step per gli slot attivi
            for (int i = 0; i < POLIMAX; i++) {
                if (glideActive[i]) computeGlideStep((uint8_t)i);
            }
            break;
        }

        // ---------- FM operator parameters ----------
        case K_FM_SIN_0: fmSetSin[fmSel][0] = (float)value / 100.0f; break;
        case K_FM_SIN_1: fmSetSin[fmSel][1] = (float)value / 100.0f; break;
        case K_FM_SIN_2: fmSetSin[fmSel][2] = (float)value / 100.0f; break;
        case K_FM_DIV_0: fmSetDiv[fmSel][0] = (int)value; break;
        case K_FM_DIV_1: fmSetDiv[fmSel][1] = (int)value; break;
        case K_FM_DIV_2: fmSetDiv[fmSel][2] = (int)value; break;

        default:
            LWS_DEBUG.printf("[B] key i32 non gestita: %c\n", key);
            break;
    }
}

// -------------------------------------------------------------------------
// 3. CALLBACK PARAM_VOCE — accettati per uniformità con SynthA
// -------------------------------------------------------------------------
// SynthB non ha voci distinte: il byte voice viene ignorato.
// Tutti i comandi sono trattati come globali.
// -------------------------------------------------------------------------
static inline void on_param_voce(char target, uint8_t voice,
                                 char key, uint8_t value) {
    if (target != MCU_ID) return;
    UNUSED(voice);
    on_param(target, key, value);
}

static inline void on_param_i32_voce(char target, uint8_t voice,
                                     char key, int32_t value) {
    if (target != MCU_ID) return;
    UNUSED(voice);
    on_param_i32(target, key, value);
}

// -------------------------------------------------------------------------
// 4. CALLBACK MIDI _V — voice ignorata
// -------------------------------------------------------------------------
// Formato accettato dal Router v0.0.5:
//   CMD_MIDI_NOTE_V : [target][voice][onoff][pitch][vel]
//   CMD_MIDI_BEND_V : [target][voice][bend_i32_le]
//   CMD_MIDI_CC_V   : [target][voice][cc][value]
// -------------------------------------------------------------------------
static inline void on_midi_note_v(char target, uint8_t voice,
                                  uint8_t onoff, uint8_t pitch, uint8_t vel) {
    if (target != MCU_ID) return;
    UNUSED(voice);
    if (onoff) onNoteOn(pitch, vel);
    else       onNoteOff(pitch);
}

static inline void on_midi_bend_v(char target, uint8_t voice, int32_t bend) {
    if (target != MCU_ID) return;
    UNUSED(voice);

    float bendf = 0.0f;
    if (bend > 0)
        bendf = (float)map(bend, 1, 8191, 0, bendMaxUpArr[bendMaxUp]) / 4000.0f;
    if (bend < 0)
        bendf = (float)map(bend, 1, -8191, 0, bendMaxDownArr[bendMaxDown]) / 4000.0f;

    frBend = bendf;
}

static inline void on_midi_cc_v(char target, uint8_t voice,
                                uint8_t cc, uint8_t value) {
    if (target != MCU_ID) return;
    UNUSED(voice);

    switch (cc) {
        case 1: // mod wheel
            modPitchLev  = map(value, 0, 127, 0, 550);
            modPitchLevA = modPitchLev - (modPitchLev * 2);
            break;
        case 3: // mod lev
            modLev  = map(value, 0, 127, 0, 1023);
            modLevA = modLev - (modLev * 2);
            break;
        case 4: // mod in B
            modInB = map(value, 0, 127, 0, 1024);
            break;
        default:
            break;
    }
}

static inline void on_error(const char *msg) {
    LWS_DEBUG.printf("[B] ERROR: %s\n", msg);
}

// -------------------------------------------------------------------------
// 5. DISPATCH LOCALE
// -------------------------------------------------------------------------
static void dispatchLocal(const LwsFrame& f) {
    switch ((char)f.cmd) {
        case CMD_PING:
            // [target_id] == MCU_ID -> PONG
            if (f.len >= 1 && (char)f.data[0] == MCU_ID) {
                lws_send_frame(LWS_PORT, MCU_ID, lws_next_seq(),
                               CMD_PONG, nullptr, 0);
                LWS_DEBUG.println("[B] PONG");
            }
            break;

        case CMD_PARAM:
            if (f.len >= 3) on_param((char)f.data[0], (char)f.data[1], f.data[2]);
            break;

        case CMD_PARAM_REL:
            if (f.len >= 3) {
                on_param((char)f.data[0], (char)f.data[1], f.data[2]);
                uint8_t ack[2] = { f.seq, f.cmd };
                lws_send_frame(LWS_PORT, MCU_ID, lws_next_seq(),
                               CMD_PARAM_ACK, ack, 2);
            }
            break;

        case CMD_PARAM_VOCE:
            if (f.len >= 4)
                on_param_voce((char)f.data[0], f.data[1],
                              (char)f.data[2], f.data[3]);
            break;

        case CMD_PARAM_I32:
            if (f.len >= 6)
                on_param_i32((char)f.data[0], (char)f.data[1],
                             lws_unpack_i32_le(&f.data[2]));
            break;

        case CMD_PARAM_I32_V:
            if (f.len >= 7)
                on_param_i32_voce((char)f.data[0], f.data[1],
                                  (char)f.data[2],
                                  lws_unpack_i32_le(&f.data[3]));
            break;

        case CMD_MIDI_NOTE_V:
            if (f.len >= 5)
                on_midi_note_v((char)f.data[0], f.data[1], f.data[2],
                               f.data[3], f.data[4]);
            break;

        case CMD_MIDI_BEND_V:
            if (f.len >= 6)
                on_midi_bend_v((char)f.data[0], f.data[1],
                               lws_unpack_i32_le(&f.data[2]));
            break;

        case CMD_MIDI_CC_V:
            if (f.len >= 4)
                on_midi_cc_v((char)f.data[0], f.data[1],
                             f.data[2], f.data[3]);
            break;

        case CMD_ERROR: {
            char msg[64] = {0};
            if (f.len >= 2) {
                size_t l = f.len - 1;
                if (l > 62) l = 62;
                memcpy(msg, &f.data[1], l);
                msg[l] = '\0';
            }
            on_error(msg);
            break;
        }

        default:
            // Ignora silenziosamente
            break;
    }
}

// -------------------------------------------------------------------------
// 6. POLL PORTA SINGOLA
// -------------------------------------------------------------------------
static LwsParser parserLws;

static void pollLws() {
    while (LWS_PORT.available() > 0) {
        uint8_t b = (uint8_t)LWS_PORT.read();
        LwsFrame f;
        if (parserLws.feed(b, f)) dispatchLocal(f);
    }
}