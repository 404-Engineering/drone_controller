#include "link.h"

#include <Arduino.h>
#include <EEPROM.h>

#if __has_include("link_key.h")
#include "link_key.h"
#else
#error "Missing include/link_key.h. Run: python3 tools/gen_link_key.py"
#endif

// The uplink counter must never repeat for a given key, even across reboots,
// or the drone would reject packets as replays (and old packets could be
// replayed). Writing flash on every packet would wear it out, so the counter
// is reserved in blocks: flash holds the start of the next unused block, and
// is rewritten once per boot and once per COUNTER_BLOCK packets (~73 min at
// 60 packets/s). A reboot skips the rest of the current block.
static const uint32_t COUNTER_BLOCK = 1UL << 18;
static const uint32_t COUNTER_MAGIC = 0x4C4E4B31;  // "LNK1"

struct CounterRecord {
  uint32_t magic;
  uint32_t next_block;
};

static uint32_t txCounter = 0;
static uint32_t reservedEnd = 0;
static bool counterExhausted = true;
static LinkReplayGuard downlinkGuard;

// Reserves [start, start + COUNTER_BLOCK) by saving its end to flash.
static bool reserveBlock(uint32_t start) {
  if (start > UINT32_MAX - COUNTER_BLOCK) {
    return false;
  }
  CounterRecord rec = {COUNTER_MAGIC, start + COUNTER_BLOCK};
  EEPROM.put(0, rec);
  if (!EEPROM.commit()) {
    return false;
  }
  txCounter = start;
  reservedEnd = rec.next_block;
  return true;
}

bool link_begin() {
  EEPROM.begin(256);
  CounterRecord rec;
  EEPROM.get(0, rec);
  // Counter 0 is never sent, so the drone's first accepted value is >= 1.
  const uint32_t start = (rec.magic == COUNTER_MAGIC) ? rec.next_block : 1;
  counterExhausted = !reserveBlock(start);
  link_replay_init(&downlinkGuard, 0, false);
  return !counterExhausted;
}

size_t link_sign_uplink(uint8_t msg_id, const uint8_t *payload, size_t len,
                        uint8_t *out) {
  if (counterExhausted) {
    return 0;
  }
  if (txCounter >= reservedEnd && !reserveBlock(reservedEnd)) {
    counterExhausted = true;
    return 0;
  }
  const size_t n = link_sign(LINK_KEY, LINK_DIR_UPLINK, msg_id, txCounter,
                             payload, len, out);
  if (n > 0) {
    txCounter++;
  }
  return n;
}

LinkVerifyResult link_verify_downlink(const uint8_t *packet, size_t len,
                                      uint8_t *msg_id,
                                      const uint8_t **payload,
                                      size_t *payload_len) {
  uint32_t counter;
  return link_verify(LINK_KEY, LINK_DIR_DOWNLINK, &downlinkGuard, packet, len,
                     msg_id, &counter, payload, payload_len);
}
