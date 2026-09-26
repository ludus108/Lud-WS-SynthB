================================================================================
LUD-WS — RIEPILOGO DI CONTINUITÀ (AGGIORNATO)
================================================================================
Documento autocontenuto. Incollalo come primo messaggio in una nuova chat
per riprendere il lavoro da dove è stato interrotto.

Ultimo aggiornamento: 2026-09-23 (VCF Wovel con 3 sottomodi)
================================================================================


-----------------------------------------------------------------------------------------------------------------------------------------
0. STRUTTURA REPOSITOTY IN gitHub
-----------------------------------------------------------------------------------------------------------------------------------------
	Lud-WS (
	|
	|(submodules)
	|_____Lud-WS-Display
	|_____Lud-WS-Router
	|_____Lud-WS-Ctrl
	|_____Lud-WS-Mod
	|_____Lud-WS-SyntA_M
	|_____Lud-WS-SynthA_V
	|_____Lud-WS-SynthA_V2
	|_____Lud-WS-SynthB
	|_____Lud-WS-Teensy
	|_____Lud-WS-Power

--------------------------------------------------------------------------------
1. ARCHITETTURA GENERALE
--------------------------------------------------------------------------------

  Display (ESP32-S3, LVGL 800x480, SD preset)
     |  Serial1 @ 1 Mbps
     v
  Router (Pico 2 RP2350)
     |
     +-- Serial1 (GP0/GP1) @ 1 Mbps ------------> Teensy 4.1 [T]
     |
     +-- SerialSynthA (SerialPIO GP2/GP3) @ 115200 -> SynthA_M [a]
     |                                                  |
     |                                                  | Serial1 @ 1 Mbps
     |                                                  v
     |                                                SynthA_V [b]
     |                                                  |
     |                                                  | Serial2 @ 1 Mbps
     |                                                  v
     |                                                SynthA_V2 [c]
     |
     +-- SerialSynthB (SerialPIO GP8/GP9) @ 115200 -> SynthB [B]
     +-- SerialCtrl   (SerialPIO GP6/GP7)  @ 115200 -> Ctrl [C]   (legacy)
     +-- SerialMod    (SerialPIO GP10/GP11) @ 115200 -> Mod [M]   (legacy)
     +-- SerialPower  (SerialPIO GP14/GP15) @ 115200 -> Power [P] (legacy)
     +-- SerialMidi   (SerialPIO GP12/GP13) @ 31250 -> MIDI IN

  Hardware target: TUTTI i synth su RP2350 (Pico 2).
  Motivo: FPU hardware Cortex-M33 → sinf/powf ~20x più veloci.
  Patch necessaria: PWM_CLKDIV_BASE = 4.8 su RP2350 (era 4.0 su RP2040).
  Clock IRQ PWM costante: 30500 Hz su entrambe le piattaforme.


--------------------------------------------------------------------------------
2. BUS LWS v1.1
--------------------------------------------------------------------------------

  Frame: [SENDER][SEQ][CMD][LEN][PAYLOAD...][CRC8][&][!]
  CRC-8/ATM (poly 0x07, init 0x00)
  Baud: 1 Mbps UART HW, 115200 SerialPIO, 31250 MIDI
  Vincolo: '&' e '!' vietati nel payload


--------------------------------------------------------------------------------
3. CMD CODES (serial_protocol.h condiviso)
--------------------------------------------------------------------------------

  CMD base:
    CMD_PING          'p'   [target]
    CMD_PONG          'P'
    CMD_PARAM         'S'   [target][key][value]
    CMD_PARAM_REL     'R'   [target][key][value] + ACK
    CMD_PARAM_ACK     'A'   [acked_seq][acked_cmd]
    CMD_ERROR         'E'   [target][msg...]
    CMD_TIMELINE      'B'   [cur_ms i32][tot_ms i32]
    CMD_MIDI_CC       'c'   [cc][value]
    CMD_MIDI_NOTE     'n'   [onoff][pitch][vel]
    CMD_MIDI_BEND     'b'   [bend i32]
    CMD_DRUM_PATTERN  'W'   [ptn_num][name...]

  CMD estesi SynthA:
    CMD_PARAM_VOCE    'V'   [target][voice][key][value]
    CMD_PARAM_I32     'I'   [target][key][i32_le]
    CMD_PARAM_I32_V   'J'   [target][voice][key][i32_le]
    CMD_MIDI_NOTE_V   'N'   [target][voice][onoff][pitch][vel]
    CMD_MIDI_BEND_V   'M'   [target][voice][bend i32_le]
    CMD_MIDI_CC_V     'K'   [target][voice][cc][value]

  CMD preset transfer:
    CMD_PRESET_BEGIN       'j'
    CMD_PRESET_CHUNK       'h'
    CMD_PRESET_END         'e'
    CMD_PRESET_ACK         'a'
    CMD_PRESET_READ        'r'
    CMD_PRESET_DUMP_BEGIN  'Q'
    CMD_PRESET_DUMP_CHUNK  'L'
    CMD_PRESET_DUMP_END    'O'


--------------------------------------------------------------------------------
4. MCU ID
--------------------------------------------------------------------------------

  'D'  Display        ESP32-S3
  'R'  Router         Pico 2 RP2350
  'a'  SynthA_M       master, 1 voce locale (voce globale 0)
  'b'  SynthA_V       slave, 2 voci locali (voci globali 1, 2)
  'c'  SynthA_V2      slave, 2 voci locali (voci globali 3, 4)
  'B'  SynthB         parafonico 6 slot
  'T'  Teensy         drum sampler + sequencer
  'C'  Ctrl, 'M' Mod, 'P' Power  (legacy)


--------------------------------------------------------------------------------
5. LUD-WS-ROUTER (Pico 2, v0.0.5) — COMPLETO
--------------------------------------------------------------------------------

  UART mapping:
    Serial1 (UART0)   GP0/GP1   @ 1 Mbps    -> Teensy
    Serial2 (UART1)   GP4/GP5   @ 1 Mbps    -> Display
    SerialSynthA      GP2/GP3   @ 115200    -> SynthA (cascata a->b->c)
    SerialSynthB      GP8/GP9   @ 115200    -> SynthB
    SerialCtrl        GP6/GP7   @ 115200
    SerialMod         GP10/GP11 @ 115200
    SerialPower       GP14/GP15 @ 115200
    SerialMidi        GP12/GP13 @ 31250

  Nodi LWS nativi: Teensy, SynthA (a, b, c)
  Nodi legacy: SynthB, Ctrl, Mod, Power

  Funzionalità: bridge Display↔Nodi, MIDI→CMD_MIDI_*_V, forwarding
  preset, discovery PING/PONG, heartbeat 1 Hz.


--------------------------------------------------------------------------------
6. LUD-WS-TEENSY (Teensy 4.1) — COMPLETO
--------------------------------------------------------------------------------

  Pin: Serial2 (7/8) @ 1 Mbps, SerialFlash CS=6, SD CS=10, SPI 11/12/13.
  Feature: drum sampler 8 voci SerialFlash + 3 track WAV SD + FX + sequencer.
  Chiavi LWS target 'T':
    R S b N K T X d f m t u M V A L P I e 1..8 Z C Q


--------------------------------------------------------------------------------
7. LUD-WS-SYNTHA (3 chip da un solo sketch) — COMPLETO
--------------------------------------------------------------------------------

  MCU_ID 'a' -> NUM_VOCI=1, VOCE_BASE=0
  MCU_ID 'b' -> NUM_VOCI=2, VOCE_BASE=1
  MCU_ID 'c' -> NUM_VOCI=2, VOCE_BASE=3

  File: Lud-WS-SynthA_M.ino, synthConfig.h, synthState.h, synthEngine.h,
        synthControl.h, synthLws.h, synthPreset.h, ottaveA.h,
        serial_protocol.h, comunicazioni_mcu.h

  Modello: 1 voce monofonica con 2 sub_voci (base + sub1 con interval
           -24..+24 e subLevel). ADSR per-voce. Note stack. Glide.
           FM operator parameters globali al chip.

  Modi: Poly (broadcast automatico dal Master) / MultiMono (indipendenti).

  Porte:
    'a': UP=SerialPIO(2,3)@115200, DOWN=Serial1@1Mbps
    'b': UP=Serial1@1Mbps, DOWN=Serial2@1Mbps
    'c': UP=Serial1@1Mbps, HAS_DOWNSTREAM=0

  Chiavi uint8 per-voce (CMD_PARAM_VOCE):
    m w c F o a k d t v l s 1 3 4 u n B M S y D r R A T g G H L O C Z X
    e 9 5 6 7 8

  Chiavi int32 per-voce (CMD_PARAM_I32_V):
    i I f j J x X Y q Q W h

  Sistema (Master): Y P Q U ! O


--------------------------------------------------------------------------------
8. LUD-WS-SYNTHB (RP2350) — IN CORSO, quasi completo
--------------------------------------------------------------------------------

  Nodo singolo, parafonico 6 slot (POLIMAX=6), wavetable unica condivisa.
  Comandi LWS con voice sempre 0.

  File:
    Lud-WS-SynthB.ino           (bootstrap + driver TLC inline + setup1/loop1)
    synthB_Config.h             (config + chiavi K_* + PWM_CLKDIV_BASE)
    synthB_State.h              (extern)
    synthB_Engine.h             (IRQ PWM, note stack, alloc slot, glide, aux ADSR)
    synthB_Control.h            (LFO mod + pitch)
    synthB_AnalogEnv.h          (ADSR hardware via pin diretti)
    synthB_Lfo3.h               (terzo LFO con 3 counter + spread)
    synthB_Vcf.h                (gestione VCF Filter/Wovel)
    synthB_Lws.h                (callback, dispatch, preset entry)
    synthPreset.h               (utility preset comune)
    ottaveB.h                   (tabelle noteArr[61], voctpow[1230])

  Pin map (RP2350):
    Serial1 (GP0/GP1) @ 115200       -> LWS verso Router
    OUTPUT_A_PIN    GP15              -> PWM audio
    PIN_ANA_ATTACK  GP2               -> 4066
    PIN_ANA_DECAY   GP3               -> 4066
    PIN_ANA_RELEASE GP4               -> 4066
    PIN_4051_A      GP5               -> CD4051 A
    PIN_4051_B      GP6               -> CD4051 B
    PIN_4051_C      GP7               -> CD4051 C
    PIN_ADC_ENV     GP26 (ADC0)       -> feedback envelope
    PIN_TLC_DATA    GP8               -> TLC5628 DATA
    PIN_TLC_CLK     GP9               -> TLC5628 CLK
    PIN_TLC_LOAD    GP10              -> TLC5628 LOAD

  TLC5628 — canali usati:
    Ch 0 = OUTA -> VCF1 cutoff
    Ch 1 = OUTB -> VCF2 cutoff
    Ch 2 = OUTC -> VCF3 cutoff
    Ch 3 = OUTD -> Resonance
    Ch 4 = OUTE -> Sustain voltage (ADSR hardware)
    Ch 5,6,7 = riservati

  Motore audio:
    - Parafonico 6 slot, allocazione dinamica, steal-glide
    - Modi mono / poly (K_SYNTHB_MODE='y')
    - Slide per-slot
    - DCO: gate secco (nessun ADSR digitale applicato al DCO)
    - Aux ADSR digitale: parametri vir_* accettati (usati per Wovel env mode)
    - Wavetable unica

  ADSR hardware:
    - 3 GPIO per ATTACK/DECAY/RELEASE
    - 3 GPIO per CD4051 (resistenze A/D/R)
    - ADC GP26 per feedback
    - State machine: IDLE -> ATTACK -> DECAY -> SUSTAIN -> RELEASE
    - Modo AD: IDLE -> ATTACK -> RELEASE (no sustain, no retrigger)
    - Soglie: ANA_ATTACK_THRESHOLD=1000, ANA_ZERO_THRESHOLD=10

  LFO:
    LFO1 Mod (timbro): sinusoide, speedMod + modLev
    LFO2 Pitch (vibrato): sinusoide, speedPitchMod + modPitchLev
    LFO3 dedicato VCF: 3 counter indipendenti, spread 0..255
      8 forme: SINE, SAW, RSAW, SQR, RND, RND50, RND25, RND10
      Output bipolare in lfo3Mod[3] (-255..+255)

  VCF mode (synthB_Vcf.h):
    FILTER (K_VCF_MODE=0), sottomodi (K_VCF_SUBMODE):
      0 = Unisono : vcf2=vcf3=vcf1, cut2/cut3 ignorati
      1 = Slave   : vcf2=vcf1+(cut2-128), vcf3=vcf1+(cut3-128)
      2 = Free    : ogni VCF indipendente

    WOVEL (K_VCF_MODE=1), sottomodi (K_WOV_SUBMODE):
      0 = pot_aeiou  : pot 0..255 -> 5 transizioni A->E->I->O->U->A
      1 = env_aeiou  : vocale A (start) -> B (sustain) -> C (release)
                      guidato da ADSR digitale o hardware
                      (K_WOV_ENV_SRC: 0=vir, 1=ana)
                      vocali settabili: K_WOV_VOWEL_A, K_WOV_VOWEL_B,
                      K_WOV_ENV_VOWEL_C
      2 = random     : vocale random su NoteOn, tempo transizione
                      K_WOV_RND_TIME_MS

    3 array formanti nodeF1/F2/F3[10] per vocali A E I O U
    Offset formant: K_WOV_FORMANT (0..500)

  Chiavi LWS (uint8, CMD_PARAM):
    m=wave_mode (0=WF, 1=FM, 2=AM)
    w=wave (0..8)
    F=fm_select (0..7)
    o=ottava (1..3)
    a=attenua (0..9)
    u=bend up, n=bend down
    5/6/7/8=vir_adsr A/D/S/R
    A=ana_attack (0..7 ch 4051)
    D=ana_decay (0..7)
    R=ana_release (0..7)
    S=ana_sustain (0..255 -> 0..1023, scritto su TLC Ch4)
    E=ana_env_mode (0=ADSR, 1=AD)
    y=synthB mode (0=mono, 1=poly)
    B=vcf mode (0=Filter, 1=Wovel)
    C=vcf submode (0=Uni, 1=Slave, 2=Free)
    1=VCF1 cut, 2=VCF2 cut, 3=VCF3 cut, 4=VCF resonance
    V=wov_vowel_A (0..255), Z=wov_vowel_B (0..255)
    H=wov_morph (0..255)
    b=lfo3 wave (0..7), c=lfo3 spread (0..255)
    K=wov_submode (0=pot, 1=env, 2=random)
    L=wov_env_vowel_C (0..255)
    M=wov_env_attack (0..255, informativo)
    g=wov_env_src (0=vir, 1=ana)
    !=panic
    P=preset select, Q=preset save

  Chiavi LWS (int32, CMD_PARAM_I32):
    i=modInB, I=modLev, f=speedMod
    j=modPitchLev, J=speedPitchMod
    h=slide time (0..1000 ms)
    x/X/Y=FM sin 0/1/2 (float*100)
    q/Q/W=FM div 0/1/2 (1..600)
    k=lfo3 rate (350..80000 us)
    l=lfo3 lev (0..1023)
    G=wov_formant (0..500)
    N=wov_rnd_time_ms (0..5000)

  Dual-core:
    loop1() rigenera mod2_wavetable[] con flag g_wt_busy per race.


--------------------------------------------------------------------------------
9. LUD-WS-DISPLAY (ESP32-S3) — IN CORSO
--------------------------------------------------------------------------------

  File:
    Lud-WS-Display.ino
    globals.h
    src/preset/preset_sd.h / .cpp        (30 preset, formato binario)
    src/preset/preset_cache.h / .cpp     (cache locale)
    src/preset/preset_transfer.h         (load/save/poly/multimono)
    src/preset/preset_ui.h               (helper UI: uiSetParam*)
    comunicazioni.h                      (patchato)
    serial_protocol.h                    (condiviso)
    src/grafica/lvglGrafFunc.cpp         (UI DCO A/B, VCF, DRUM, ecc.)
    src/grafica/lvglPreset.cpp           (selector preset, rename)
    images.c, images/

  Sistema preset:
    SD: /preset/synthA/preset_00.bin .. preset_29.bin
        /preset/synthA/nomi_presetA.txt
        /preset/synthB/preset_00.bin .. preset_29.bin
        /preset/synthB/nomi_presetB.txt

  Formato binario K-V:
    [version=0x01][key][len][value...]...[0xFF][crc8]

  Cache locale (Strada A):
    struct PresetCache { int32_t value[256]; uint8_t type[256]; }
    PresetCache cacheA[5]  (una per voce SynthA)
    PresetCache cacheB     (SynthB)

  Helper UI (preset_ui.h):
    uiSetParamU8(voice, key, value)
    uiSetParamI32(voice, key, value)
    uiSetParamB_U8(key, value)
    uiSetParamB_I32(key, value)

  Funzioni transfer:
    sendBlobToVoice, loadPresetToVoice
    loadPresetSynthA_Poly, loadPresetSynthA_MultiMono, loadPresetSynthB
    savePresetA, savePresetB

  Wrapper nel .ino:
    requestPresetLoad, requestPresetSave, requestRenameApply

  Pagina DCO A/B:
    - 3 categorie (WF, FM, AM) -> K_MODE
    - Griglia waveform -> K_WAVEFORM (mapping UI->FW per SynthB)
    - Slider SHAPE -> K_MOD_IN_B
    - ui2fw_wave[9] = {0, 4, 3, 2, 1, 5, 6, 7, 8}


--------------------------------------------------------------------------------
10. PYTHON GENERATOR PRESET
--------------------------------------------------------------------------------

  Script che genera i file .bin su SD.
  Entry: struct.pack('<'+fmt, value) con fmt 'b','h','H','i','I'
  Genera 30 preset SynthA + 30 preset SynthB + 2 file nomi.

  Template SynthA: Wavefold Lead, FM Base, Arp Pad
  Template SynthB: Poly Default, Mono Slide, Pluck AD

  DA AGGIORNARE: aggiungere chiavi VCF mode, LFO3, ADSR hardware, Wovel.


--------------------------------------------------------------------------------
11. RP2350 — PATCH NECESSARIE
--------------------------------------------------------------------------------

  Ogni sketch .ino e Config.h:

    #if defined(PICO_RP2350) || defined(ARDUINO_ARCH_RP2350)
      #define PWM_CLKDIV_BASE   4.8f
    #else
      #define PWM_CLKDIV_BASE   4.0f
    #endif

    #define PWM_IRQ_RATE_HZ   30500.0f

  Nel .ino: const float masterFreq = PWM_CLKDIV_BASE; (era 4.0f)

  Arduino IDE 2.x:
    Board: Raspberry Pi Pico 2 (o Pico 2 W se con WiFi)
    CPU Speed: 150 MHz
    Optimize: -O3 (per FPU)
    USB Stack: Pico SDK
    PSRAM: Disabled
    Debug Port: Serial (o Disabled)

  Pin occupati da Pico 2 W (WiFi): GP23, GP24, GP25, GP29
  -> Nel progetto NON sono usati, nessun conflitto.


--------------------------------------------------------------------------------
12. COSE DA FARE (TODO)
--------------------------------------------------------------------------------

  CRITICHE:
  1. Aggiornare Python generator per nuove chiavi SynthB:
     VCF mode, LFO3, ADSR hardware, Wovel, env_src.
  2. Compilare e testare SynthB con le ultime modifiche.
  3. Verificare race IRQ vs loop1 (g_wt_busy correttamente usato?).

  FEATURE NON IMPLEMENTATE:
  - DSP effetti (chiavi accettate, non applicate)
  - MIDI mode split (parametri accettati)
  - Preset dump (CMD_PRESET_READ stub)
  - Periferiche SynthB (encoder, bottoni, display locale?)
  - Revisione ADSR (tutti i synth)

  IN SOSPESO:
  - Test hardware end-to-end (a -> b -> c -> B -> T)
  - Preset transfer da SD reale
  - Verifica FPU su RP2350 (pitch 440 Hz)
  - Collisione 'Q' tra K_FM_DIV_1 e K_PRESET_SAVE (in namespace diversi)


--------------------------------------------------------------------------------
13. CONVENZIONI
--------------------------------------------------------------------------------

  Voce globale SynthA: 0..4
  Voce locale SynthA:  0..NUM_VOCI-1
  Sub_voce SynthA:     0..1 (2 per voce)
  POLIMAX SynthB:      6 (slot parafonici)
  Voice=0 fisso per SynthB

  Chiavi K_*: uint8 (nome 1 char) o int32 (int32_le)
  Payload CMD_MIDI_*_V: [target][voice][...]
  Poly broadcast: solo Master 'a' in modalità Poly
  Bridge relay: 'a' <-> 'b' <-> 'c'


--------------------------------------------------------------------------------
14. HARDWARE ESTERNO SYNTHB
--------------------------------------------------------------------------------

  TLC5628 (DAC octal 8-bit, 3 fili DATA/CLK/LOAD):
    Parola 11 bit MSB-first [A2 A1 A0 D7..D0]
    5 canali usati: VCF1/VCF2/VCF3/Res/Sustain

  CD4051 (mux 8 canali):
    Seleziona resistenza A/D/R dell'ADSR hardware
    3 pin select (A, B, C) pilotati dal SynthB

  CD4066 (4 switch analogici):
    3 switch per ATTACK / DECAY / RELEASE
    Pilotati da 3 GPIO del SynthB

  ADC GP26:
    Feedback envelope hardware (0..1023)

  AD5242: NON usato (rimosso).


--------------------------------------------------------------------------------
15. PROSSIMO STEP
--------------------------------------------------------------------------------

  Se hardware montato:
    - Test SynthB end-to-end con VCF Filter/Wovel
    - Verifica TLC5628 con oscilloscopio su OUT1..OUT5
    - Test ADSR hardware (gate virtuale da NoteOn/NoteOff)
    - Test Wovel nei 3 submodi (pot, env, random)

  Se non montato:
    A) Aggiornare Python generator con nuove chiavi SynthB
    B) Riscrivere pagina DRUM Display
    C) Aggiungere DSP effetti
    D) Revisione ADSR (SynthA + SynthB)

================================================================================
FINE RIEPILOGO
================================================================================