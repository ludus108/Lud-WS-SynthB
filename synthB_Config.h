#pragma once

// =========================================================================
// synthB_Config.h — Configurazione e chiavi LWS del SynthB
// =========================================================================
// SynthB è parafonico: uno strumento, 6 slot interni.
// Non ha voci distinte, quindi i CMD estesi usano sempre voice=0.
// =========================================================================

// -------------------------------------------------------------------------
// 1. IDENTITÀ DEL CHIP
// -------------------------------------------------------------------------
#ifndef MCU_ID
  #define MCU_ID 'B'
#endif

// Polyphony interna (slot parafonici)
#define POLIMAX 6

// -------------------------------------------------------------------------
// 2. PORTA LWS
// -------------------------------------------------------------------------
// SynthB è nodo singolo, collegato direttamente al Router.
// Serial1 = UART0 hardware (GP0/GP1 di default).
// -------------------------------------------------------------------------
#define LWS_PORT       Serial1
#define LWS_BAUD       115200UL

// -------------------------------------------------------------------------
// PIN HARDWARE
// -------------------------------------------------------------------------
#define OUTPUT_A_PIN     15    // PWM audio

// ADSR hardware — pin diretti
#define PIN_ANA_ATTACK   2     // → 4066
#define PIN_ANA_DECAY    3     // → 4066
#define PIN_ANA_RELEASE  4     // → 4066
#define PIN_4051_A       5     // → CD4051 A
#define PIN_4051_B       6     // → CD4051 B
#define PIN_4051_C       7     // → CD4051 C
#define PIN_ADC_ENV      26    // ADC0 envelope feedback
#define K_WOV_ENV_SRC      'g'   // uint8: 0=ADSR digitale (vir), 1=ADSR hardware (ana)

// -------------------------------------------------------------------------
// 4. COSTANTI MOTORE
// -------------------------------------------------------------------------
#if defined(PICO_RP2350) || defined(ARDUINO_ARCH_RP2350)
  #define PWM_CLKDIV_BASE   4.8f
#else
  #define PWM_CLKDIV_BASE   4.0f
#endif

#define PWM_IRQ_RATE_HZ   30500.0f

// old #define PWM_IRQ_RATE_HZ   30000.0f   // ~freq IRQ con clkdiv=4, wrap=1023

#define MAX_PITCH         127
#define NOTE_STACK_MAX    8           // per-mono
#define MAX_NOTEARRAY_IDX 60          // noteArr 61 elementi

#ifndef PIx2
  #define PIx2  6.28318530718f
#endif

#define UNUSED(x)  ((void)(x))

// -------------------------------------------------------------------------
// 5. SICUREZZA CMD ESTESI (se serial_protocol.h non aggiornato)
// -------------------------------------------------------------------------
#ifndef CMD_PARAM_VOCE
  #define CMD_PARAM_VOCE   'V'
#endif
#ifndef CMD_PARAM_I32
  #define CMD_PARAM_I32    'I'
#endif
#ifndef CMD_PARAM_I32_V
  #define CMD_PARAM_I32_V  'J'
#endif
#ifndef CMD_MIDI_NOTE_V
  #define CMD_MIDI_NOTE_V  'N'
#endif
#ifndef CMD_MIDI_BEND_V
  #define CMD_MIDI_BEND_V  'M'
#endif
#ifndef CMD_MIDI_CC_V
  #define CMD_MIDI_CC_V    'K'
#endif

// -------------------------------------------------------------------------
// TLC5628 (DAC octal 8-bit, SPI a 3 fili)
// -------------------------------------------------------------------------
#define PIN_TLC_DATA   8
#define PIN_TLC_CLK    9
#define PIN_TLC_LOAD   10

// -------------------------------------------------------------------------
// 6. CHIAVI LWS — GLOBALI CHIP (CMD_PARAM)
// -------------------------------------------------------------------------
// Formato: [target='B'][key][value]
// -------------------------------------------------------------------------

// MODE / WAVEFORM
#define K_MODE            'm'   // 0=WF, 1=FM, 2=AM
#define K_WAVEFORM        'w'   // 0..8 (WF/AM) o 0..7 (FM)
#define K_FM_SELECT       'F'   // 0..7 → fmSel (quale set FM)

// TUNING
#define K_OTTAVA          'o'   // 1..3
#define K_ATTENUA         'a'   // 0..9, accettato ma NON applicato (residuo)

// BENDER
#define K_BEND_UP         'u'   // 0..4
#define K_BEND_DOWN       'n'   // 0..4

// --- ADSR digitale ausiliario (vir) ---
#define K_VIR_ADSR_A   '5'   // 0..255
#define K_VIR_ADSR_D   '6'
#define K_VIR_ADSR_S   '7'
#define K_VIR_ADSR_R   '8'

// --- ADSR hardware (ana) ---
#define K_ANA_ADSR_A   'A'   // 0..7 canale 4051
#define K_ANA_ADSR_D   'D'   // 0..7
#define K_ANA_ADSR_S   'S'   // 0..255 (mappato in 0..1023)
#define K_ANA_ADSR_R   'R'   // 0..7
#define K_ANA_ENV_MODE 'E'   // 0=ADSR, 1=AD

// --- LFO 3 ---
#define K_LFO3_WAVE     'b'   // 0..7 (uint8)
#define K_LFO3_SPREAD   'c'   // 0..255 (uint8)
#define K_LFO3_RATE     'k'   // 350..80000 (int32)
#define K_LFO3_LEV      'l'   // 0..1023 (int32)

/* // --- ex VCF cutoff base ---
#define K_VCF1_CUT      'C'   // 0..255 (uint8)
#define K_VCF2_CUT      'G'   // 0..255 (uint8)
#define K_VCF3_CUT      'H'   // 0..255 (uint8) */

// --- VCF cutoff base ---
#define K_VCF1_CUT     '1'   // 0..255
#define K_VCF2_CUT     '2'   // 0..255
#define K_VCF3_CUT     '3'   // 0..255
#define K_VCF_RES      '4'   // 0..255

// MODALITÀ
#define K_SYNTHB_MODE     'y'   // 0=mono, 1=poly

// SISTEMA
#define K_PANIC           '!'   // qualsiasi valore → all notes off
#define K_PRESET_SEL      'P'   // 0..29
#define K_PRESET_SAVE     'Q'   // 0..29

// -------------------------------------------------------------------------
// 7. CHIAVI LWS — GLOBALI CHIP (CMD_PARAM_I32)
// -------------------------------------------------------------------------
// Formato: [target='B'][key][i32_le]
// -------------------------------------------------------------------------

// LFO mod
#define K_MOD_IN_B        'i'   // 0..1023
#define K_MOD_LEV         'I'   // 0..1023
#define K_LFO_RATE        'f'   // 350..80000 µs (inverso)

// LFO pitch
#define K_PITCH_LEV       'j'   // 0..550
#define K_PITCH_RATE      'J'   // 350..80000 µs (inverso)

// SLIDE
#define K_SLIDE_TIME      'h'   // 0..1000 ms (int32)

// FM operator parameters
#define K_FM_SIN_0        'x'   // float*100
#define K_FM_SIN_1        'X'
#define K_FM_SIN_2        'Y'
#define K_FM_DIV_0        'q'   // int, 1..600
#define K_FM_DIV_1        'Q'
#define K_FM_DIV_2        'W'