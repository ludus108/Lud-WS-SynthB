================================================================================
LUD-WS — RIEPILOGO DI CONTINUITÀ (AGGIORNATO)
================================================================================
Documento autocontenuto. Incollalo come primo messaggio in una nuova chat
per riprendere il lavoro da dove è stato interrotto.

-----------------------------------------------------------------------------------------------------------------------------------------
0. STRUTTURA REPOSITOTY IN gitHub
-----------------------------------------------------------------------------------------------------------------------------------------
	Lud-WS 
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


Ultimo aggiornamento: 2026-09-23 (SynthB (nome repo in gitHub = Lud-WS-SynthB) con VCF mode + LFO3 + ADSR hardware)
================================================================================


--------------------------------------------------------------------------------
1. ARCHITETTURA GENERALE
--------------------------------------------------------------------------------

  Display (ESP32-S3)
     |  Serial1 @ 1 Mbps
     v
  Router (Pico 2 RP2350)
     |
     +-- Serial1 (GP0/GP1) @ 1 Mbps ----> Teensy 4.1 [T]   (drum sampler)
     |
     +-- SerialSynthA (SerialPIO GP2/GP3) @ 115200 ----> SynthA_M [a]
     |                                                      |
     |                                                      | Serial1 @ 1 Mbps
     |                                                      v
     |                                                    SynthA_V [b]
     |                                                      |
     |                                                      | Serial2 @ 1 Mbps
     |                                                      v
     |                                                    SynthA_V2 [c]
     |
     +-- SerialSynthB (SerialPIO GP8/GP9) @ 115200 -----> SynthB [B]
     +-- SerialCtrl   (SerialPIO GP6/GP7)  @ 115200 -----> Ctrl [C]   (legacy)
     +-- SerialMod    (SerialPIO GP10/GP11) @ 115200 ----> Mod [M]    (legacy)
     +-- SerialPower  (SerialPIO GP14/GP15) @ 115200 ----> Power [P]  (legacy)
     +-- SerialMidi   (SerialPIO GP12/GP13) @ 31250 -----> MIDI IN hardware

  Hardware target aggiornato: TUTTI i synth su RP2350 (Pico 2).
  Motivo: FPU hardware Cortex-M33 → sinf/powf ~20x più veloci.
  Patch necessaria: PWM_CLKDIV_BASE = 4.8 su RP2350 (era 4.0 su RP2040).


--------------------------------------------------------------------------------
2. BUS LWS v1.1
--------------------------------------------------------------------------------

  Frame: [SENDER][SEQ][CMD][LEN][PAYLOAD...][CRC8][&][!]
  CRC-8/ATM (poly 0x07, init 0x00)
  Baud: 1 Mbps UART HW, 115200 SerialPIO, 31250 MIDI


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

  'D'  Display        ESP32-S3 (LVGL 800x480, SD preset)
  'R'  Router         Pico 2 RP2350
  'a'  SynthA_M       master, 1 voce locale (voce globale 0)
  'b'  SynthA_V       slave, 2 voci locali (voci globali 1, 2)
  'c'  SynthA_V2      slave, 2 voci locali (voci globali 3, 4)
  'B'  SynthB         parafonico 6 slot
  'T'  Teensy         drum sampler + sequencer
  'C'  Ctrl           legacy
  'M'  Mod            legacy
  'P'  Power          legacy


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
  Nodi legacy: SynthB, Ctrl, Mod, Power (protocollo 'p' + '&!')

  Funzionalità:
    - Bridge Display <-> Nodi
    - MIDI hardware -> CMD_MIDI_*_V verso SynthA + telemetria al Display
    - Forwarding CMD preset
    - Discovery PING/PONG
    - Heartbeat 1 Hz verso Display

  Funzioni chiave:
    isLwsNode(), portForNode(), targetForVoice()
    forwardPingToNode(), forwardParamToNode(), forwardMidiVToSynthA()
    pollNodePorts() (bridge nodi LWS -> Display)


--------------------------------------------------------------------------------
6. LUD-WS-TEENSY (Teensy 4.1) — COMPLETO
--------------------------------------------------------------------------------

  Pin map:
    Serial2 (pin 7 RX, 8 TX) @ 1 Mbps    -> LWS verso Router
    SerialFlash CS = 6                    -> drum samples
    SD CS = 10                            -> tracce WAV + pattern TXT
    SPI: MISO=12, MOSI=11, SCK=13
    I2S/I2C: da Audio Shield

  Feature:
    - Drum sampler 8 voci (AudioPlaySerialflashRaw)
    - 3 track WAV SD (AudioPlaySdWav x2)
    - Mixer + FX (filter, delay, reverb)
    - Sequencer 16 pattern x 8 voci x 32 step
    - Pattern names via file /PTN0.NAME .. /PTN15.NAME

  Chiavi LWS target 'T':
    R=run, S=stop, b=bpm, N=pattern, K=kit, T=track, X=sync
    d=drum lev, f=fx lev, m=mono lev, t=trk2 lev, u=trk3 lev
    M=mute mask, V=voice sel, A=sample, L=level, P=pan, I=revint
    e=fx preset, 1-8=fx params, Z=status, C=cpu, Q=pattern info query


--------------------------------------------------------------------------------
7. LUD-WS-SYNTHA (3 chip da un solo sketch) — COMPLETO
--------------------------------------------------------------------------------

  MCU_ID 'a'  ->  NUM_VOCI=1, VOCE_BASE=0
  MCU_ID 'b'  ->  NUM_VOCI=2, VOCE_BASE=1
  MCU_ID 'c'  ->  NUM_VOCI=2, VOCE_BASE=3

  File:
    Lud-WS-SynthA_M.ino       (bootstrap + setup1/loop1 dual-core)
    synthConfig.h              (config + PWM_CLKDIV_BASE)
    synthState.h               (extern)
    synthEngine.h              (IRQ, ADSR, note stack, wavetable, glide)
    synthControl.h             (LFO mod/pitch per-voce, arp, gater)
    synthLws.h                 (callback, bridge, Poly broadcast, preset)
    synthPreset.h              (utility preset comune)
    ottaveA.h                  (tabelle noteArr[3][61], voctpow[1230],
                                bendMaxUpArr, bendMaxDownArr)

  Modello voci:
    - Ogni voce è monofonica con note stack
    - Ogni voce ha 2 sub_voci: base + sub1 (interval -24..+24, subLevel)
    - ADSR per-voce (A D S R, 0..255)
    - Glide/slide per-voce (0..1 s)
    - FM operator parameters globali al chip (fmSetSin/Div[8][3])

  Modi:
    Poly (K_SYNTH_MODE='Y' = 0): tutte le voci stesso preset, broadcast auto
    MultiMono (='Y' = 1): ogni voce indipendente

  Porte:
    'a': LWS_PORT_UP = SerialPIO(2,3) @ 115200; DOWN = Serial1 @ 1 Mbps
    'b': UP = Serial1 @ 1 Mbps; DOWN = Serial2 @ 1 Mbps
    'c': UP = Serial1 @ 1 Mbps; HAS_DOWNSTREAM = 0

  Chiavi LWS per-voce uint8 (CMD_PARAM_VOCE):
    m=mode, w=waveform sub0, c=waveform sub1, F=fm select, o=ottava
    a=attenua, k=tracking, d=detune, t=tempo slide
    v=vcf wave, l=vcf lfo lev, s=vcf lfo rate, 1=vcf lfo sync
    3=vcf multi, 4=vcf start
    u=bend up, n=bend down, B=send bend, M=send mod
    S=slide, y=lfo mod sync, D=dsp, r=arp on, R=arp mode, A=arp multi
    T=arp octaves, g=gater on, G=gater num, H=gate multi, L=gate lung
    O=conta offset, C=midi ch, Z=nota split, X=midi mode
    e=sub1 level, 9=voice vol, 5/6/7/8=ADSR A/D/S/R

  Chiavi LWS per-voce int32 (CMD_PARAM_I32_V):
    i=modInB, I=modLev, f=speedMod, j=modPitchLev, J=speedPitchMod
    x/X/Y=FM sin 0/1/2 (float*100), q/Q/W=FM div 0/1/2
    h=sub1 interval (-24..+24)

  Chiavi globali chip (CMD_PARAM_I32): e=calib sel, E=calib val

  Chiavi sistema (CMD_PARAM, solo Master):
    Y=synth mode, P=preset sel, Q=preset save, U=voice mute
    !=panic, O=all notes off


--------------------------------------------------------------------------------
8. LUD-WS-SYNTHB (RP2350) — IN CORSO, quasi completo
--------------------------------------------------------------------------------

  Nodo singolo, parafonico 6 slot (POLIMAX=6), wavetable unica condivisa.
  Non ha voci distinte: CMD estesi con voice sempre 0.

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

  Modello audio:
    - Parafonico: 6 slot indipendenti con allocazione dinamica
    - Wavetable UNICA
    - Modi: mono (1 nota + glide) / poly (6 note + steal-glide)
    - Slide per-slot (fresh=jump, stolen=glide)
    - DCO: gate secco (nessun ADSR digitale applicato)
    - Aux ADSR digitale: parametri accettati (vir_*) ma non applicati

  ADSR hardware (ana_*):
    - 3 GPIO per ATTACK/DECAY/RELEASE (verso 4066)
    - 3 GPIO per selezione canale CD4051 (resistenze A/D/R)
    - 1 ADC per feedback envelope (GP26)
    - Sustain via TLC5628 (Ch 4)
    - State machine: IDLE -> ATTACK -> DECAY -> SUSTAIN -> RELEASE
    - Modo AD: IDLE -> ATTACK -> RELEASE (no sustain, no retrigger)
    - Soglie: ANA_ATTACK_THRESHOLD=1000, ANA_ZERO_THRESHOLD=10

  LFO sistema (3 LFO totali):
    - LFO1 Mod (timbro): sinusoide, speedMod + modLev
    - LFO2 Pitch (vibrato): sinusoide, speedPitchMod + modPitchLev
    - LFO3 dedicato VCF: 3 counter indipendenti, stessa forma, spread 0..255
      8 forme: SINE, SAW, RSAW, SQR, RND, RND50, RND25, RND10
      - spread=0: counter in fase
      - spread=255: counter 0 a 0°, 1 a 90°, 2 a 180°
      - RND*: valori indipendenti per counter con probabilità di aggiornamento

  VCF mode (synthB_Vcf.h):
    - FILTER mode, sottomodi:
      * Unisono: vcf2=vcf3=vcf1, cut2/cut3 ignorati
      * Slave:   vcf2=vcf1+offset2, vcf3=vcf1+offset3
                 (offset bidirezionale: 128 = centro, 0..255 = ±127)
      * Free:    ogni VCF indipendente
    - WOVEL mode:
      * 3 array nodeF1/F2/F3[10] = frequenze di taglio per vocali A E I O U
      * Morph tra vocale A e vocale B (0..255)
      * Formant offset (0..500) aggiunto ai 3 cut
    - LFO3 modula i 3 cut finali (lfo3Mod[3] bipolar)
    - vcfTick() calcola e scrive su TLC5628

  Chiavi LWS (uint8, CMD_PARAM):
    m=mode (0=WF, 1=FM, 2=AM)
    w=waveform (0..8)
    F=fm select (0..7)
    o=ottava (1..3)
    a=attenua (0..9)
    u=bend up, n=bend down
    5/6/7/8=vir_adsr A/D/S/R (non applicato al DCO)
    A=ana_attack (0..7 ch 4051)
    D=ana_decay (0..7)
    R=ana_release (0..7)
    S=ana_sustain (0..255, mappato 0..1023 + scritto su TLC Ch4)
    E=ana_env_mode (0=ADSR, 1=AD)
    y=synthB mode (0=mono, 1=poly)
    B=vcf mode (0=Filter, 1=Wovel)
    C=vcf submode (0=Unisono, 1=Slave, 2=Free)
    1=VCF1 cut, 2=VCF2 cut, 3=VCF3 cut, 4=VCF resonance
    V=wov_vowel_A (0..255), Z=wov_vowel_B (0..255)
    H=wov_morph (0..255)
    b=lfo3 wave (0..7), c=lfo3 spread (0..255)
    !=panic
    P=preset select, Q=preset save

  Chiavi LWS (int32, CMD_PARAM_I32):
    i=modInB, I=modLev, f=speedMod
    j=modPitchLev, J=speedPitchMod
    h=slide time (0..1000 ms)
    x/X/Y=FM sin 0/1/2 (float*100)
    q/Q/W=FM div 0/1/2 (1..600)
    k=lfo3 rate (350..80000 µs)
    l=lfo3 lev (0..1023)
    G=wov_formant (0..500)

  Dual-core:
    setup1() vuoto
    loop1(): rigenera mod2_wavetable[] in base a mode/waveform/mod
             con flag g_wt_busy per evitare race con IRQ


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
        /preset/synthA/nomi_presetA.txt  (30 righe)
        /preset/synthB/preset_00.bin .. preset_29.bin
        /preset/synthB/nomi_presetB.txt

  Formato binario K-V:
    [version=0x01]
    [key][len][value...]  ripetuto
    [0xFF]
    [crc8]

  Cache locale (Strada A):
    struct PresetCache { int32_t value[256]; uint8_t type[256]; }
    PresetCache cacheA[5]   (una per voce SynthA)
    PresetCache cacheB      (SynthB)

  Helper UI (preset_ui.h):
    uiSetParamU8(voice, key, value)      -> SynthA voce
    uiSetParamI32(voice, key, value)
    uiSetParamB_U8(key, value)           -> SynthB
    uiSetParamB_I32(key, value)

  Funzioni transfer:
    sendBlobToVoice(target, voice, buf, len)
    loadPresetToVoice(target, voice, presetId, PresetCache*)
    loadPresetSynthA_Poly(presetId)
    loadPresetSynthA_MultiMono(map[5])
    loadPresetSynthB(presetId)
    savePresetA(voice, presetId)
    savePresetB(presetId)

  Wrapper nel .ino (per UI):
    requestPresetLoad(synth, presetId)
    requestPresetSave(synth, presetId)
    requestRenameApply(synth, presetId, newName)

  Pagina DCO A/B:
    - 3 categorie (WF, FM, AM) -> K_MODE
    - Griglia waveform -> K_WAVEFORM (con mapping UI->FW per SynthB)
    - Slider SHAPE -> K_MOD_IN_B
    - Mapping UI 0..8 -> firmware per SynthB:
      ui2fw_wave[9] = {0, 4, 3, 2, 1, 5, 6, 7, 8}
      (SAW=0, SAW8=OCT-SAW, TRI=3, SQR=2, SINE=1, FM1-3=5-7, NOISE=8)


--------------------------------------------------------------------------------
10. PYTHON GENERATOR PRESET
--------------------------------------------------------------------------------

  Script Python che genera i file .bin dalla SD.
  Formato entry: struct.pack('<'+fmt, value) con fmt 'b','h','H','i','I'
  Genera 30 preset SynthA + 30 preset SynthB + 2 file nomi.

  Template attuali SynthA: Wavefold Lead, FM Base, Arp Pad
  Template attuali SynthB: Poly Default, Mono Slide, Pluck AD

  DA AGGIORNARE: aggiungere chiavi VCF mode, LFO3, ADSR hardware, Wovel.


--------------------------------------------------------------------------------
11. RP2350 — PATCH NECESSARIE
--------------------------------------------------------------------------------

  Ogni sketch (.ino) e Config.h deve avere:

    #if defined(PICO_RP2350) || defined(ARDUINO_ARCH_RP2350)
      #define PWM_CLKDIV_BASE   4.8f
    #else
      #define PWM_CLKDIV_BASE   4.0f
    #endif

    #define PWM_IRQ_RATE_HZ   30500.0f   (costante tra le due piattaforme)

  E nel .ino: const float masterFreq = PWM_CLKDIV_BASE; (era 4.0f)

  Arduino IDE 2.x:
    Board: Raspberry Pi Pico 2 (o Pico 2 W se con WiFi)
    CPU Speed: 150 MHz
    Optimize: -O3 (per FPU)
    USB Stack: Pico SDK
    PSRAM: Disabled
    Debug Port: Serial (o Disabled)

  Pin occupati da Pico 2 W (versione WiFi): GP23, GP24, GP25, GP29
  → Nel progetto NON sono usati, nessun conflitto.


--------------------------------------------------------------------------------
12. COSE DA FARE (TODO)
--------------------------------------------------------------------------------

  CRITICHE:
  1. Aggiornare Python generator per nuove chiavi SynthB
     (VCF mode, LFO3, ADSR hardware, Wovel).
  2. Compilare e testare SynthB con le ultime modifiche (VCF, LFO3, TLC).
  3. Verificare che l'IRQ PWM sul SynthB usi g_wt_busy nell'IRQ (race).

  FEATURE NON IMPLEMENTATE:
  - DSP effetti (chiavi accettate, non applicate)
  - MIDI mode split (parametri accettati)
  - Preset dump (CMD_PRESET_READ stub)
  - Morph dinamico Wovel guidato da envelope
  - Vowel random mode (rimosso dal .ino originale)
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
  Voice=0 fisso per SynthB (nodo singolo)

  Chiavi K_*: uint8 (nome 1 char), alcune int32 (int32_le)
  Payload CMD_MIDI_*_V: [target][voice][...]
  Poly broadcast: solo Master 'a' in modalità Poly
  Bridge relay: 'a' <-> 'b' <-> 'c'


--------------------------------------------------------------------------------
14. HARDWARE ESTERNO SYNTHB
--------------------------------------------------------------------------------

  TLC5628 (DAC octal 8-bit):
    - 8 canali, usati 5
    - Interfaccia seriale 3 fili (DATA/CLK/LOAD)
    - Parola 11 bit MSB-first: [A2 A1 A0 D7..D0]

  CD4051 (mux analogico 8 canali):
    - Seleziona resistenza per A/D/R dell'ADSR hardware
    - 3 pin di selezione (A, B, C)

  CD4066 (switch analogici):
    - 3 switch per ATTACK / DECAY / RELEASE
    - Pilotati da 3 GPIO del SynthB

  ADC GP26:
    - Legge il feedback dell'envelope hardware (0..1023)

  AD5242: NON usato (rimosso dal progetto).


--------------------------------------------------------------------------------
15. PROSSIMO STEP
--------------------------------------------------------------------------------

  Se hardware montato:
    - Test SynthB end-to-end con nuovo VCF mode
    - Verifica TLC5628 con oscilloscopio su OUT1..OUT5
    - Test ADSR hardware (gate virtuale da NoteOn/NoteOff)

  Se non montato:
    A) Aggiornare Python generator con nuove chiavi
    B) Riscrivere pagina DRUM Display (label pattern)
    C) Aggiungere morph dinamico Wovel
    D) Aggiungere DSP effetti
    E) Revisione ADSR (SynthA + SynthB)

================================================================================
FINE RIEPILOGO
================================================================================