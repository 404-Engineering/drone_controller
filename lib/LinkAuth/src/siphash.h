#pragma once

#include <stddef.h>
#include <stdint.h>

// SipHash-2-4 (Aumasson & Bernstein), a keyed PRF built for short messages.
// Returns the 64-bit tag for `len` bytes of `data` under a 128-bit `key`.
uint64_t siphash24(const uint8_t key[16], const uint8_t *data, size_t len);
