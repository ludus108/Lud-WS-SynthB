#pragma once
#include "synthB_Config.h"
#include "synthB_State.h"

// =========================================================================
// synthB_AnalogEnv.h — Controllo ADSR hardware (pin diretti)
// =========================================================================
// Pilota un envelope hardware esterno:
//   - ATTACK/DECAY/RELEASE  → 3 GPIO (verso 4066 o ingresso logico)
//   - CD4051 A/B/C          → 3 GPIO (selezione resistenza)
//   - ADC0                  → feedback envelope (0..1023)
//
// Il "gate" è virtuale: lo genera il SynthB su NoteOn/NoteOff.
// =========================================================================

// --- Bit mapping (nella variabile ana_outputByte) ---
#define ANA_BIT_ATTACK    0
#define ANA_BIT_DECAY     1
#define ANA_BIT_RELEASE   2
#define ANA_BIT_4051_A    3
#define ANA_BIT_4051_B    4
#define ANA_BIT_4051_C    5

// --- State machine ---
enum AnaEnvState : uint8_t {
    ANA_IDLE = 0,
    ANA_ATTACK,
    ANA_DECAY,
    ANA_SUSTAIN,
    ANA_RELEASE
};

// --- Stato interno ---
static uint8_t  ana_outputByte  = 0;
static uint8_t  ana_state       = ANA_IDLE;
static bool     ana_lastGate    = false;
static uint32_t ana_lastUpdate  = 0;
static bool     ana_releaseForced = false;

// Soglie ADC
static const int ANA_ATTACK_THRESHOLD = 1000;
static const int ANA_ZERO_THRESHOLD   = 10;

// -------------------------------------------------------------------------
// LOW-LEVEL: scrittura simultanea dei 6 output
// -------------------------------------------------------------------------
// Legge i bit di ana_outputByte e li applica ai 6 GPIO.
// Se in futuro si passa al 74HC595, basta sostituire questa funzione.
// -------------------------------------------------------------------------
static inline void anaWriteOutputs() {
    digitalWrite(PIN_ANA_ATTACK,  (ana_outputByte >> ANA_BIT_ATTACK ) & 1);
    digitalWrite(PIN_ANA_DECAY,   (ana_outputByte >> ANA_BIT_DECAY  ) & 1);
    digitalWrite(PIN_ANA_RELEASE, (ana_outputByte >> ANA_BIT_RELEASE) & 1);
    digitalWrite(PIN_4051_A,      (ana_outputByte >> ANA_BIT_4051_A ) & 1);
    digitalWrite(PIN_4051_B,      (ana_outputByte >> ANA_BIT_4051_B ) & 1);
    digitalWrite(PIN_4051_C,      (ana_outputByte >> ANA_BIT_4051_C ) & 1);
}

static void anaSet4051Channel(uint8_t ch) {
    ch &= 0x07;
    ana_outputByte &= ~((1 << ANA_BIT_4051_A) |
                        (1 << ANA_BIT_4051_B) |
                        (1 << ANA_BIT_4051_C));
    if (ch & 1) ana_outputByte |= (1 << ANA_BIT_4051_A);
    if (ch & 2) ana_outputByte |= (1 << ANA_BIT_4051_B);
    if (ch & 4) ana_outputByte |= (1 << ANA_BIT_4051_C);
    anaWriteOutputs();
}

static void anaSetAttack(bool on) {
    if (on) ana_outputByte |=  (1 << ANA_BIT_ATTACK);
    else    ana_outputByte &= ~(1 << ANA_BIT_ATTACK);
    anaWriteOutputs();
}

static void anaSetDecay(bool on) {
    if (on) ana_outputByte |=  (1 << ANA_BIT_DECAY);
    else    ana_outputByte &= ~(1 << ANA_BIT_DECAY);
    anaWriteOutputs();
}

static void anaSetRelease(bool on) {
    if (on) ana_outputByte |=  (1 << ANA_BIT_RELEASE);
    else    ana_outputByte &= ~(1 << ANA_BIT_RELEASE);
    anaWriteOutputs();
}

// Variante ottimizzata: scrive tutti i bit in un colpo solo.
// Utile per cambi di fase (es. passaggio ATTACK → DECAY).
static void anaSetPhase(bool attack, bool decay, bool release,
                        uint8_t ch4051) {
    ana_outputByte &= 0b11000111;   // azzera i 6 bit gestiti
    if (attack)  ana_outputByte |= (1 << ANA_BIT_ATTACK);
    if (decay)   ana_outputByte |= (1 << ANA_BIT_DECAY);
    if (release) ana_outputByte |= (1 << ANA_BIT_RELEASE);
    ch4051 &= 0x07;
    if (ch4051 & 1) ana_outputByte |= (1 << ANA_BIT_4051_A);
    if (ch4051 & 2) ana_outputByte |= (1 << ANA_BIT_4051_B);
    if (ch4051 & 4) ana_outputByte |= (1 << ANA_BIT_4051_C);
    anaWriteOutputs();
}

// -------------------------------------------------------------------------
// INIT
// -------------------------------------------------------------------------
static void initAnalogEnv() {
    pinMode(PIN_ANA_ATTACK,  OUTPUT);
    pinMode(PIN_ANA_DECAY,   OUTPUT);
    pinMode(PIN_ANA_RELEASE, OUTPUT);
    pinMode(PIN_4051_A,      OUTPUT);
    pinMode(PIN_4051_B,      OUTPUT);
    pinMode(PIN_4051_C,      OUTPUT);
    pinMode(PIN_ADC_ENV,     INPUT);

    ana_outputByte = 0;
    anaWriteOutputs();

    ana_state         = ANA_IDLE;
    ana_lastGate      = false;
    ana_releaseForced = false;

    LWS_DEBUG.println("[B] AnalogEnv init OK (direct pins)");
}

// -------------------------------------------------------------------------
// GATE VIRTUALE
// -------------------------------------------------------------------------
static void analogEnvNoteOn() {
    ana_lastGate = true;

    // In AD no-retrigger, se non siamo IDLE non ricominciare
    if (ana_env_mode == 1 && ana_state != ANA_IDLE) return;

    // Start/retrigger
    anaSetPhase(true, false, false, ana_attack);
    ana_state      = ANA_ATTACK;
    ana_lastUpdate = millis();
    ana_releaseForced = false;

    LWS_DEBUG.printf("[B] ANA ATTACK ch=%u\n", ana_attack);
}

static void analogEnvNoteOff() {
    ana_lastGate = false;

    // Solo in ADSR classico: vai in RELEASE
    if (ana_env_mode == 0 && ana_state != ANA_IDLE) {
        anaSetPhase(false, false, true, ana_release);
        ana_state      = ANA_RELEASE;
        ana_lastUpdate = millis();

        LWS_DEBUG.printf("[B] ANA RELEASE ch=%u\n", ana_release);
    }
    // In AD mode: NoteOff ignorato (la state machine va a zero da sola)
}

// -------------------------------------------------------------------------
// STATE MACHINE
// -------------------------------------------------------------------------
static void analogEnvProcess() {
    int envADC = analogRead(PIN_ADC_ENV);

    switch (ana_state) {

        case ANA_ATTACK:
            if (envADC > ANA_ATTACK_THRESHOLD) {
                if (ana_env_mode == 0) {
                    // ADSR: → DECAY
                    anaSetPhase(false, true, false, ana_decay);
                    ana_state = ANA_DECAY;
                    LWS_DEBUG.printf("[B] ANA DECAY ch=%u\n", ana_decay);
                } else {
                    // AD: → RELEASE
                    anaSetPhase(false, false, true, ana_release);
                    ana_state = ANA_RELEASE;
                    LWS_DEBUG.printf("[B] ANA RELEASE (AD) ch=%u\n", ana_release);
                }
            }
            break;

        case ANA_DECAY:
            if (envADC <= ana_sustain) {
                // → SUSTAIN: tutti gli switch OFF
                anaSetPhase(false, false, false, ana_decay);
                ana_state = ANA_SUSTAIN;
                LWS_DEBUG.printf("[B] ANA SUSTAIN lvl=%u\n", ana_sustain);
            }
            break;

        case ANA_SUSTAIN:
            // Attende NoteOff
            break;

        case ANA_RELEASE:
            if (envADC <= ANA_ZERO_THRESHOLD) {
                anaSetPhase(false, false, false, ana_release);
                ana_state = ANA_IDLE;
                LWS_DEBUG.println("[B] ANA IDLE");
            }
            break;

        case ANA_IDLE:
        default:
            break;
    }
}

// -------------------------------------------------------------------------
// UPDATE (chiamato da loop() ogni ~1 ms)
// -------------------------------------------------------------------------
static void analogEnvUpdate() {
    // In IDLE: forza RELEASE la prima volta per scaricare la capacità
    if (ana_state == ANA_IDLE) {
        if (!ana_releaseForced) {
            anaSetRelease(true);
            ana_releaseForced = true;
        }
        return;
    }

    uint32_t now = millis();
    if (now - ana_lastUpdate >= 1) {
        ana_lastUpdate = now;
        analogEnvProcess();
    }
}