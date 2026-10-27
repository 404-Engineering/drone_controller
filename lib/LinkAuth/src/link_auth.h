#pragma once

// Message authentication for the NRF24 link between the vehicle controller and
// the drone. Every radio packet is wrapped as:
//
//   offset  size  field
//   0       1     msg_id
//   1       4     counter   uint32 LE; strictly increasing per sender
//   5       N     payload   N <= LINK_MAX_PAYLOAD (23)
//   5+N     4     tag       low 32 bits of SipHash-2-4, LE
//
// tag = SipHash-2-4(key, direction || msg_id || counter || payload)
//
// The direction byte is not sent; it stops a packet from one direction being
// replayed in the other (e.g. drone telemetry reflected back as an uplink).
// The counter gives replay protection: a receiver only accepts a counter
// greater than the last one it accepted. The low byte of the counter doubles
// as the old 8-bit `seq` for gap detection.

#include <stddef.h>
#include <stdint.h>

#define LINK_KEY_LEN      16
#define LINK_PACKET_MAX   32  // NRF24 payload limit
#define LINK_HEADER_LEN   5   // msg_id + counter
#define LINK_TAG_LEN      4
#define LINK_OVERHEAD     (LINK_HEADER_LEN + LINK_TAG_LEN)
#define LINK_MAX_PAYLOAD  (LINK_PACKET_MAX - LINK_OVERHEAD)

enum LinkDirection : uint8_t {
  LINK_DIR_UPLINK   = 0x01,  // vehicle controller -> drone
  LINK_DIR_DOWNLINK = 0x02,  // drone -> vehicle controller (ACK payloads)
};

enum LinkVerifyResult {
  LINK_OK = 0,
  LINK_ERR_LENGTH,   // shorter than header + tag, or longer than 32 bytes
  LINK_ERR_TAG,      // wrong key, wrong direction, or corrupted / forged
  LINK_ERR_REPLAY,   // counter not newer than the last accepted packet
};

// Builds a signed packet in `out` (at least LINK_PACKET_MAX bytes).
// Returns the packet length, or 0 if `len` > LINK_MAX_PAYLOAD.
size_t link_sign(const uint8_t key[LINK_KEY_LEN], LinkDirection dir,
                 uint8_t msg_id, uint32_t counter,
                 const uint8_t *payload, size_t len, uint8_t *out);

// Replay state on the receiving side, one per sender.
struct LinkReplayGuard {
  uint32_t last_counter;  // highest counter accepted so far
  bool seen_any;          // false until the first packet is accepted
};

// Starts a guard that only accepts counters above `floor`. Use 0 with
// seen_any = false on first boot; a receiver that persists its last accepted
// counter can pass it here so old packets stay rejected after a reboot.
void link_replay_init(LinkReplayGuard *guard, uint32_t floor, bool seen_any);

// Checks the tag, then the counter. On LINK_OK, updates `guard` and points
// `*payload` / `*payload_len` into `packet`, and sets `*msg_id` / `*counter`.
// A packet that fails the tag check never touches the replay state.
LinkVerifyResult link_verify(const uint8_t key[LINK_KEY_LEN], LinkDirection dir,
                             LinkReplayGuard *guard,
                             const uint8_t *packet, size_t packet_len,
                             uint8_t *msg_id, uint32_t *counter,
                             const uint8_t **payload, size_t *payload_len);
