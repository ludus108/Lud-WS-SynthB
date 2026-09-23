#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"
#include "synthB_Engine.h"
#include "synthB_Control.h"
#include "synthB_AnalogEnv.h"
#include "synthB_Lfo3.h"
#include "synthPreset.h"

// Forward declaration: synthB_presetEntry è definita a fine file
// ma usata da dispatchLocal() più sopra.
static void synthB_presetEntry(uint8_t key, uint8_t len, const uint8_t* value);
// =========================================================================
// synthB_Lws.h — Layer LWS del SynthB
// =========================================================================
// SynthB è nodo singolo:
//   - Una sola porta LWS (Serial1)
//   - Nessun bridge, nessun broadcast
//   - I CMD MIDI *_V arrivano con target/voice (voice ignorata)
//
// Usa tlcWrite() e TLC_CH_* definite inline nel .ino.
// =========================================================================

// -------------------------------------------------------------------------
// 1. CALLBACK — CMD_PARAM (uint8)
// -------------------------------------------------------------------------
static inline void on_param(char target, char key, uint8_t value) {
    if (target != MCU_ID) return;
    LWS_DEBUG.printf("[B] PARAM key=%c val=%u\n", key, value);

    switch (key) {

        // ---------- MODE / WAVEFORM ----------
		case K_WOV_ENV_SRC:
    wovEnvSrc = (value != 0) ? 1 : 0;
    break;
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
        case K_OTTAVA:
            ottava = value;
            switch (value) {
                case 1:  oct_sw = 1; break;
                case 2:  oct_sw = 2; break;
                case 3:  oct_sw = 4; break;
                default: oct_sw = 2; break;
            }
            break;
        case K_ATTENUA:
            attenua = value;
            break;

        // ---------- BENDER ----------
        case K_BEND_UP:   bendMaxUp   = value; break;
        case K_BEND_DOWN: bendMaxDown = value; break;

        // ---------- ADSR digitale (vir) ----------
        case K_VIR_ADSR_A: vir_adsr_a = value; break;
        case K_VIR_ADSR_D: vir_adsr_d = value; break;
        case K_VIR_ADSR_S: vir_adsr_s = value; break;
        case K_VIR_ADSR_R: vir_adsr_r = value; break;

        // ---------- ADSR hardware (ana) ----------
        case K_ANA_ADSR_A: ana_attack  = constrain(value, 0, 7); break;
        case K_ANA_ADSR_D: ana_decay   = constrain(value, 0, 7); break;
        case K_ANA_ADSR_R: ana_release = constrain(value, 0, 7); break;
        case K_ANA_ADSR_S:
            ana_sustain = map(value, 0, 255, 0, 1023);
            tlcWrite(TLC_CH_SUSTAIN, value);   // DAC canale 5
            break;
        case K_ANA_ENV_MODE:
            ana_env_mode = (value != 0) ? 1 : 0;
            break;

        // ---------- VCF cutoff base (TLC) ----------
        case K_VCF1_CUT:
            vcf1Base = value;
            tlcWrite(TLC_CH_VCF1, vcf1Base);
            break;
        case K_VCF2_CUT:
            vcf2Base = value;
            tlcWrite(TLC_CH_VCF2, vcf2Base);
            break;
        case K_VCF3_CUT:
            vcf3Base = value;
            tlcWrite(TLC_CH_VCF3, vcf3Base);
            break;
        case K_VCF_RES:
            tlcWrite(TLC_CH_RES, value);
            break;

        // ---------- LFO3 ----------
        case K_LFO3_WAVE:
            lfo3Wave = (uint8_t)constrain(value, 0, 7);
            break;
        case K_LFO3_SPREAD:
            lfo3Spread = value;
            break;

        // ---------- MODALITÀ ----------
        case K_SYNTHB_MODE:
            synthBMode = (value != 0) ? 1 : 0;
            onAllNotesOff();
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
// 2. CALLBACK — CMD_PARAM_I32 (int32)
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
            for (int i = 0; i < POLIMAX; i++) {
                if (glideActive[i]) computeGlideStep((uint8_t)i);
            }
            break;
        }

        // ---------- FM operator (globali chip) ----------
        case K_FM_SIN_0: fmSetSin[fmSel][0] = (float)value / 100.0f; break;
        case K_FM_SIN_1: fmSetSin[fmSel][1] = (float)value / 100.0f; break;
        case K_FM_SIN_2: fmSetSin[fmSel][2] = (float)value / 100.0f; break;
        case K_FM_DIV_0: fmSetDiv[fmSel][0] = (int)value; break;
        case K_FM_DIV_1: fmSetDiv[fmSel][1] = (int)value; break;
        case K_FM_DIV_2: fmSetDiv[fmSel][2] = (int)value; break;

        // ---------- LFO3 ----------
        case K_LFO3_RATE:
            lfo3Rate = constrain((int32_t)value, 350, 80000);
            break;
        case K_LFO3_LEV:
            lfo3Lev = constrain((int32_t)value, 0, 1023);
            break;

        default:
            LWS_DEBUG.printf("[B] key i32 non gestita: %c\n", key);
            break;
    }
}

// -------------------------------------------------------------------------
// 3. Wrapper voce (SynthB non ha voci: voice ignorata)
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
// 4. CALLBACK MIDI _V
// -------------------------------------------------------------------------
// CMD_MIDI_NOTE_V : [target][voice][onoff][pitch][vel]
// CMD_MIDI_BEND_V : [target][voice][bend_i32_le]
// CMD_MIDI_CC_V   : [target][voice][cc][value]
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

        // ---------- PING ----------
        case CMD_PING:
            if (f.len >= 1 && (char)f.data[0] == MCU_ID) {
                lws_send_frame(LWS_PORT, MCU_ID, lws_next_seq(),
                               CMD_PONG, nullptr, 0);
                LWS_DEBUG.println("[B] PONG");
            }
            break;

        // ---------- PARAM standard ----------
        case CMD_PARAM:
            if (f.len >= 3)
                on_param((char)f.data[0], (char)f.data[1], f.data[2]);
            break;

        case CMD_PARAM_REL:
            if (f.len >= 3) {
                on_param((char)f.data[0], (char)f.data[1], f.data[2]);
                uint8_t ack[2] = { f.seq, f.cmd };
                lws_send_frame(LWS_PORT, MCU_ID, lws_next_seq(),
                               CMD_PARAM_ACK, ack, 2);
            }
            break;

        // ---------- PARAM estesi ----------
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

        // ---------- MIDI ----------
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

        // ---------- PRESET ----------
        case CMD_PRESET_BEGIN:
            if (f.len >= 5) {
                uint8_t voice = f.data[1];
                uint8_t id    = f.data[2];
                uint16_t len  = (uint16_t)f.data[3] | ((uint16_t)f.data[4] << 8);
                uint8_t status;
                if (len > PRESET_MAX_LEN) {
                    status = PRESET_ACK_LEN_FAIL;
                } else {
                    presetBegin(voice, id, len);
                    status = PRESET_ACK_OK;
                }
                uint8_t ack[3] = { f.data[0], voice, status };
                lws_send_frame(LWS_PORT, MCU_ID, lws_next_seq(),
                               CMD_PRESET_ACK, ack, 3);
                LWS_DEBUG.printf("[B] preset BEGIN v=%u len=%u -> %u\n",
                                 voice, len, status);
            }
            break;

        case CMD_PRESET_CHUNK:
            if (f.len >= 5) {
                uint8_t voice = f.data[1];
                uint16_t off  = (uint16_t)f.data[2] | ((uint16_t)f.data[3] << 8);
                uint8_t dlen  = f.len - 4;
                presetChunk(voice, off, &f.data[4], dlen);
            }
            break;

        case CMD_PRESET_END:
            if (f.len >= 3) {
                uint8_t voice = f.data[1];
                uint8_t crc   = f.data[2];
                uint8_t st    = presetEnd(voice, crc);
                if (st == PRESET_ACK_OK) {
                    uint8_t rc = presetApply(synthB_presetEntry);
                    if (rc != 0) st = rc;
                }
                uint8_t ack[3] = { f.data[0], voice, st };
                lws_send_frame(LWS_PORT, MCU_ID, lws_next_seq(),
                               CMD_PRESET_ACK, ack, 3);
                LWS_DEBUG.printf("[B] preset END v=%u -> %u\n", voice, st);
            }
            break;

        case CMD_PRESET_READ:
            // (fase 2: dump del preset corrente)
            break;

        // ---------- ERROR ----------
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
            // ignora silenziosamente
            break;
    }
}

// -------------------------------------------------------------------------
// 6. POLL PORTA
// -------------------------------------------------------------------------
static LwsParser parserLws;

static void pollLws() {
    while (LWS_PORT.available() > 0) {
        uint8_t b = (uint8_t)LWS_PORT.read();
        LwsFrame f;
        if (parserLws.feed(b, f)) dispatchLocal(f);
    }
}

// -------------------------------------------------------------------------
// 7. PRESET ENTRY HANDLER
// -------------------------------------------------------------------------
// Chiamato una volta per ogni entry K-V del preset.
// -------------------------------------------------------------------------
static void synthB_presetEntry(uint8_t key, uint8_t len, const uint8_t* value) {

    switch (key) {
case K_WOV_ENV_SRC:
    if (len>=1) wovEnvSrc = (presetRd8(value) != 0) ? 1 : 0;
    break;
        // ---------- uint8: mode / waveform / tuning ----------
        case K_MODE:          if (len>=1) { mode = presetRd8(value);       wavetable_setup(); } break;
        case K_WAVEFORM:      if (len>=1) { waveform = presetRd8(value);   wavetable_setup(); } break;
        case K_FM_SELECT:     if (len>=1) fmSel = presetRd8(value); break;
        case K_OTTAVA:        if (len>=1) {
                                  ottava = presetRd8(value);
                                  oct_sw = (ottava==1)?1 : (ottava==3)?4 : 2;
                              } break;
        case K_ATTENUA:       if (len>=1) attenua = presetRd8(value); break;
        case K_BEND_UP:       if (len>=1) bendMaxUp   = presetRd8(value); break;
        case K_BEND_DOWN:     if (len>=1) bendMaxDown = presetRd8(value); break;

        // ---------- ADSR digitale ----------
        case K_VIR_ADSR_A:    if (len>=1) vir_adsr_a = presetRd8(value); break;
        case K_VIR_ADSR_D:    if (len>=1) vir_adsr_d = presetRd8(value); break;
        case K_VIR_ADSR_S:    if (len>=1) vir_adsr_s = presetRd8(value); break;
        case K_VIR_ADSR_R:    if (len>=1) vir_adsr_r = presetRd8(value); break;

        // ---------- ADSR hardware ----------
        case K_ANA_ADSR_A:    if (len>=1) ana_attack  = constrain(presetRd8(value), 0, 7); break;
        case K_ANA_ADSR_D:    if (len>=1) ana_decay   = constrain(presetRd8(value), 0, 7); break;
        case K_ANA_ADSR_R:    if (len>=1) ana_release = constrain(presetRd8(value), 0, 7); break;
        case K_ANA_ADSR_S:    if (len>=1) {
                                  uint8_t v = presetRd8(value);
                                  ana_sustain = map(v, 0, 255, 0, 1023);
                                  tlcWrite(TLC_CH_SUSTAIN, v);
                              } break;
        case K_ANA_ENV_MODE:  if (len>=1) ana_env_mode = (presetRd8(value) != 0) ? 1 : 0; break;

        // ---------- VCF cutoff / res (via TLC) ----------
        case K_VCF1_CUT:      if (len>=1) { vcf1Base = presetRd8(value);
                                            tlcWrite(TLC_CH_VCF1, vcf1Base); } break;
        case K_VCF2_CUT:      if (len>=1) { vcf2Base = presetRd8(value);
                                            tlcWrite(TLC_CH_VCF2, vcf2Base); } break;
        case K_VCF3_CUT:      if (len>=1) { vcf3Base = presetRd8(value);
                                            tlcWrite(TLC_CH_VCF3, vcf3Base); } break;
        case K_VCF_RES:       if (len>=1) tlcWrite(TLC_CH_RES, presetRd8(value)); break;

        // ---------- LFO3 ----------
        case K_LFO3_WAVE:     if (len>=1) lfo3Wave   = constrain(presetRd8(value), 0, 7); break;
        case K_LFO3_SPREAD:   if (len>=1) lfo3Spread = presetRd8(value); break;

        // ---------- Modo (mono/poly) ----------
        case K_SYNTHB_MODE:
            if (len>=1) {
                synthBMode = presetRd8(value) ? 1 : 0;
                onAllNotesOff();
            }
            break;

        // ---------- int32 / uint16 ----------
        case K_MOD_IN_B:      if (len>=2) modInB = presetRd16(value); break;
        case K_MOD_LEV:       if (len>=2) {
                                  modLev  = presetRd16(value);
                                  modLevA = modLev - (modLev*2);
                              } break;
        case K_PITCH_LEV:     if (len>=2) {
                                  modPitchLev  = presetRd16(value);
                                  modPitchLevA = modPitchLev - (modPitchLev*2);
                              } break;

        case K_LFO_RATE:      if (len>=4) speedMod      = presetRd32(value); break;
        case K_PITCH_RATE:    if (len>=4) speedPitchMod = presetRd32(value); break;
        case K_SLIDE_TIME:    if (len>=4) {
                                  uint32_t t = presetRd32(value);
                                  if (t > 1000) t = 1000;
                                  slideTimeMs = t;
                              } break;
        case K_LFO3_RATE:     if (len>=4) lfo3Rate = constrain((int32_t)presetRd32(value), 350, 80000); break;
        case K_LFO3_LEV:      if (len>=4) lfo3Lev  = constrain((int32_t)presetRd32(value), 0, 1023); break;

        // ---------- FM operator ----------
        case K_FM_SIN_0:      if (len>=2) fmSetSin[fmSel][0] = (float)(int16_t)presetRd16(value)/100.0f; break;
        case K_FM_SIN_1:      if (len>=2) fmSetSin[fmSel][1] = (float)(int16_t)presetRd16(value)/100.0f; break;
        case K_FM_SIN_2:      if (len>=2) fmSetSin[fmSel][2] = (float)(int16_t)presetRd16(value)/100.0f; break;
        case K_FM_DIV_0:      if (len>=2) fmSetDiv[fmSel][0] = presetRd16(value); break;
        case K_FM_DIV_1:      if (len>=2) fmSetDiv[fmSel][1] = presetRd16(value); break;
        case K_FM_DIV_2:      if (len>=2) fmSetDiv[fmSel][2] = presetRd16(value); break;

        default:
            // chiave sconosciuta: ignora silenziosamente
            break;
    }
}