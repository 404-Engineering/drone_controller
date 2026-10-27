#include "siphash.h"

static inline uint64_t rotl(uint64_t x, int b) {
  return (x << b) | (x >> (64 - b));
}

static inline uint64_t load64_le(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) {
    v = (v << 8) | p[i];
  }
  return v;
}

#define SIPROUND                                                 \
  do {                                                           \
    v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);    \
    v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;                       \
    v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;                       \
    v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);    \
  } while (0)

uint64_t siphash24(const uint8_t key[16], const uint8_t *data, size_t len) {
  const uint64_t k0 = load64_le(key);
  const uint64_t k1 = load64_le(key + 8);

  uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
  uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
  uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
  uint64_t v3 = 0x7465646279746573ULL ^ k1;

  // Full 8-byte blocks: 2 compression rounds each
  const size_t full = len & ~(size_t)7;
  for (size_t i = 0; i < full; i += 8) {
    const uint64_t m = load64_le(data + i);
    v3 ^= m;
    SIPROUND;
    SIPROUND;
    v0 ^= m;
  }

  // Last block: remaining bytes, with the message length in the top byte
  uint64_t b = (uint64_t)len << 56;
  for (size_t i = 0; i < (len & 7); i++) {
    b |= (uint64_t)data[full + i] << (8 * i);
  }
  v3 ^= b;
  SIPROUND;
  SIPROUND;
  v0 ^= b;

  // Finalization: 4 rounds
  v2 ^= 0xff;
  SIPROUND;
  SIPROUND;
  SIPROUND;
  SIPROUND;

  return v0 ^ v1 ^ v2 ^ v3;
}
