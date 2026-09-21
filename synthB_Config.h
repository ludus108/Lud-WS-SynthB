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
// 3. PIN HARDWARE
// -------------------------------------------------------------------------
#define OUTPUT_A_PIN   15      // PWM audio
// OUTPUT_ON_PIN: non definito per ora, aggiunto quando si conosceranno
// le periferiche (verrà dal nuovo hardware)

// -------------------------------------------------------------------------
// 4. COSTANTI MOTORE
// -------------------------------------------------------------------------
#define PWM_IRQ_RATE_HZ   30000.0f   // ~freq IRQ con clkdiv=4, wrap=1023

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
#define K_ATTENUA         'a'   // 0..9 (indice attenuaNumArr)

// BENDER
#define K_BEND_UP         'u'   // 0..4
#define K_BEND_DOWN       'n'   // 0..4

// ADSR ausiliario (parametri accettati ma NON applicati al DCO per ora)
#define K_ADSR_A          '5'   // 0..255
#define K_ADSR_D          '6'   // 0..255
#define K_ADSR_S          '7'   // 0..255
#define K_ADSR_R          '8'   // 0..255

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