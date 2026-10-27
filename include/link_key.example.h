#pragma once

// Template for include/link_key.h, the 128-bit key shared by this controller
// and its drone. Don't put a real key in this file. Generate one with:
//
//   python3 tools/gen_link_key.py
//
// and copy the generated include/link_key.h to the drone's radio firmware.
// link_key.h is gitignored; keep it out of version control.

#include <stdint.h>

static const uint8_t LINK_KEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
