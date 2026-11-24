#ifndef SECRETS_H
#define SECRETS_H

#include <Arduino.h>

// ─── Template for secrets.h ─────────────────────────────
// 1. Copy this file and rename the copy to secrets.h
// 2. Replace the zeros with your own random 16-byte key and 12-byte IV
// 3. Use the SAME values on every node (base station and handhelds)
// secrets.h is listed in .gitignore and is never uploaded.

static const uint8_t AES_KEY[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t IV_BASE[12] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#endif
