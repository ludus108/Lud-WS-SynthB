#pragma once
#include <Arduino.h>
#include "serial_protocol.h"

// =========================================================================
// synthPreset.h — Utility per il trasferimento preset (comune)
// =========================================================================
// Buffer + parser K-V. Non conosce i dettagli del synth:
// il chiamante passa un handler per ogni entry letta.
//
// Formato preset (blob binario):
//   [version=0x01]
//   [k1][l1][v1...]
//   [k2][l2][v2...]
//   ...
//   [0xFF]              terminatore
//   [crc8]              CRC-8/ATM su tutto ciò che precede
// =========================================================================

#define PRESET_MAX_LEN   512
#define PRESET_VERSION   0x01

// Status ACK (deve combaciare col Display)
#define PRESET_ACK_OK         0
#define PRESET_ACK_CRC_FAIL   1
#define PRESET_ACK_LEN_FAIL   2
#define PRESET_ACK_VER_FAIL   3
#define PRESET_ACK_BUSY       4

// ---- Stato trasferimento (singolo per chip) ----
static uint8_t  presetBuf[PRESET_MAX_LEN];
static uint16_t presetLen     = 0;
static uint16_t presetRcvd    = 0;
static uint8_t  presetVoice   = 0xFF;
static uint8_t  presetId      = 0;
static bool     presetActive  = false;

// -------------------------------------------------------------------------
// BEGIN
// -------------------------------------------------------------------------
static void presetBegin(uint8_t voice, uint8_t id, uint16_t len) {
    if (len > PRESET_MAX_LEN) {
        presetActive = false;
        return;
    }
    presetVoice  = voice;
    presetId     = id;
    presetLen    = len;
    presetRcvd   = 0;
    presetActive = true;
    memset(presetBuf, 0, len);
}

// -------------------------------------------------------------------------
// CHUNK
// -------------------------------------------------------------------------
static void presetChunk(uint8_t voice, uint16_t off,
                        const uint8_t* data, uint8_t dataLen) {
    if (!presetActive)        return;
    if (voice != presetVoice) return;
    if (off + dataLen > presetLen) return;   // overflow: ignora
    memcpy(&presetBuf[off], data, dataLen);
    if (off + dataLen > presetRcvd) presetRcvd = off + dataLen;
}

// -------------------------------------------------------------------------
// END — verifica CRC
// -------------------------------------------------------------------------
static uint8_t presetEnd(uint8_t voice, uint8_t crc) {
    if (!presetActive)           return PRESET_ACK_BUSY;
    if (voice != presetVoice)    return PRESET_ACK_BUSY;
    if (presetRcvd < presetLen)  return PRESET_ACK_LEN_FAIL;

    uint8_t computed = lws_crc8(presetBuf, presetLen);
    if (computed != crc)         return PRESET_ACK_CRC_FAIL;
    return PRESET_ACK_OK;
}

// -------------------------------------------------------------------------
// APPLY — itera le entry K-V
// -------------------------------------------------------------------------
// Ritorna:
//   0                        = ok
//   PRESET_ACK_VER_FAIL      = version non riconosciuta
//   PRESET_ACK_LEN_FAIL      = formato malformato
// -------------------------------------------------------------------------
typedef void (*presetEntryHandler)(uint8_t key, uint8_t len,
                                   const uint8_t* value);

static uint8_t presetApply(presetEntryHandler handler) {
    if (!presetActive) return PRESET_ACK_BUSY;
    if (presetLen < 2) return PRESET_ACK_LEN_FAIL;

    uint16_t pos = 0;
    uint8_t version = presetBuf[pos++];
    if (version != PRESET_VERSION) {
        presetActive = false;
        return PRESET_ACK_VER_FAIL;
    }

    while (pos < presetLen) {
        uint8_t key = presetBuf[pos++];
        if (key == 0xFF) break;              // terminatore

        if (pos >= presetLen) { presetActive = false; return PRESET_ACK_LEN_FAIL; }
        uint8_t len = presetBuf[pos++];

        if (pos + len > presetLen) { presetActive = false; return PRESET_ACK_LEN_FAIL; }

        handler(key, len, &presetBuf[pos]);
        pos += len;
    }

    presetActive = false;
    return 0;
}

// -------------------------------------------------------------------------
// Lettura helper LE
// -------------------------------------------------------------------------
static inline uint8_t  presetRd8 (const uint8_t* p) { return p[0]; }
static inline uint16_t presetRd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t presetRd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}