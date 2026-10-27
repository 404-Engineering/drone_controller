#include "link_auth.h"

#include <string.h>

#include "siphash.h"

static void put_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Tag over direction || header || payload, where `body` is header + payload.
static uint32_t compute_tag(const uint8_t key[LINK_KEY_LEN], LinkDirection dir,
                            const uint8_t *body, size_t body_len) {
  uint8_t buf[1 + LINK_PACKET_MAX];
  buf[0] = (uint8_t)dir;
  memcpy(buf + 1, body, body_len);
  return (uint32_t)siphash24(key, buf, 1 + body_len);
}

// Compares without an early exit, so timing doesn't reveal how much matched.
static bool tags_equal(const uint8_t *a, const uint8_t *b) {
  uint8_t diff = 0;
  for (size_t i = 0; i < LINK_TAG_LEN; i++) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0;
}

size_t link_sign(const uint8_t key[LINK_KEY_LEN], LinkDirection dir,
                 uint8_t msg_id, uint32_t counter,
                 const uint8_t *payload, size_t len, uint8_t *out) {
  if (len > LINK_MAX_PAYLOAD) {
    return 0;
  }

  out[0] = msg_id;
  put_u32_le(out + 1, counter);
  if (len > 0) {
    memcpy(out + LINK_HEADER_LEN, payload, len);
  }

  const size_t body_len = LINK_HEADER_LEN + len;
  put_u32_le(out + body_len, compute_tag(key, dir, out, body_len));
  return body_len + LINK_TAG_LEN;
}

void link_replay_init(LinkReplayGuard *guard, uint32_t floor, bool seen_any) {
  guard->last_counter = floor;
  guard->seen_any = seen_any;
}

LinkVerifyResult link_verify(const uint8_t key[LINK_KEY_LEN], LinkDirection dir,
                             LinkReplayGuard *guard,
                             const uint8_t *packet, size_t packet_len,
                             uint8_t *msg_id, uint32_t *counter,
                             const uint8_t **payload, size_t *payload_len) {
  if (packet_len < LINK_OVERHEAD || packet_len > LINK_PACKET_MAX) {
    return LINK_ERR_LENGTH;
  }

  const size_t body_len = packet_len - LINK_TAG_LEN;
  uint8_t expected[LINK_TAG_LEN];
  put_u32_le(expected, compute_tag(key, dir, packet, body_len));
  if (!tags_equal(expected, packet + body_len)) {
    return LINK_ERR_TAG;
  }

  const uint32_t ctr = get_u32_le(packet + 1);
  if (guard->seen_any && ctr <= guard->last_counter) {
    return LINK_ERR_REPLAY;
  }

  guard->last_counter = ctr;
  guard->seen_any = true;

  *msg_id = packet[0];
  *counter = ctr;
  *payload = packet + LINK_HEADER_LEN;
  *payload_len = body_len - LINK_HEADER_LEN;
  return LINK_OK;
}
