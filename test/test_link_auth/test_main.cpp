#include <string.h>
#include <unity.h>

#include <link_auth.h>
#include <siphash.h>

static const uint8_t KEY[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static const uint8_t OTHER_KEY[16] = {0xff};
static const uint8_t PAYLOAD[] = {0x10, 0x20, 0x30, 0x40, 0x50};

static uint8_t pkt[LINK_PACKET_MAX];
static size_t pkt_len;
static LinkReplayGuard guard;

void setUp() {
  link_replay_init(&guard, 0, false);
  pkt_len = link_sign(KEY, LINK_DIR_UPLINK, 0x04, 100, PAYLOAD, sizeof(PAYLOAD), pkt);
}

void tearDown() {}

static LinkVerifyResult verify(const uint8_t *key, LinkDirection dir,
                               const uint8_t *p, size_t len) {
  uint8_t msg_id;
  uint32_t counter;
  const uint8_t *payload;
  size_t payload_len;
  return link_verify(key, dir, &guard, p, len, &msg_id, &counter, &payload, &payload_len);
}

// Reference vectors from the SipHash paper (key 00..0f, message 00..len-1).
void test_siphash_reference_vectors() {
  uint8_t msg[15];
  for (int i = 0; i < 15; i++) msg[i] = (uint8_t)i;
  TEST_ASSERT_EQUAL_HEX64(0x726fdb47dd0e0e31ULL, siphash24(KEY, msg, 0));
  TEST_ASSERT_EQUAL_HEX64(0xa129ca6149be45e5ULL, siphash24(KEY, msg, 15));
}

void test_roundtrip() {
  TEST_ASSERT_EQUAL(LINK_OVERHEAD + sizeof(PAYLOAD), pkt_len);

  uint8_t msg_id;
  uint32_t counter;
  const uint8_t *payload;
  size_t payload_len;
  TEST_ASSERT_EQUAL(LINK_OK, link_verify(KEY, LINK_DIR_UPLINK, &guard, pkt, pkt_len,
                                         &msg_id, &counter, &payload, &payload_len));
  TEST_ASSERT_EQUAL_HEX8(0x04, msg_id);
  TEST_ASSERT_EQUAL_UINT32(100, counter);
  TEST_ASSERT_EQUAL(sizeof(PAYLOAD), payload_len);
  TEST_ASSERT_EQUAL_MEMORY(PAYLOAD, payload, sizeof(PAYLOAD));
}

void test_full_size_packet() {
  uint8_t big[LINK_MAX_PAYLOAD] = {0};
  TEST_ASSERT_EQUAL(LINK_PACKET_MAX,
                    link_sign(KEY, LINK_DIR_UPLINK, 1, 1, big, sizeof(big), pkt));
  TEST_ASSERT_EQUAL(LINK_OK, verify(KEY, LINK_DIR_UPLINK, pkt, LINK_PACKET_MAX));
}

void test_rejects_oversize_payload() {
  uint8_t big[LINK_MAX_PAYLOAD + 1] = {0};
  TEST_ASSERT_EQUAL(0, link_sign(KEY, LINK_DIR_UPLINK, 1, 1, big, sizeof(big), pkt));
}

void test_rejects_every_flipped_bit() {
  for (size_t i = 0; i < pkt_len * 8; i++) {
    uint8_t copy[LINK_PACKET_MAX];
    memcpy(copy, pkt, pkt_len);
    copy[i / 8] ^= (uint8_t)(1 << (i % 8));
    TEST_ASSERT_EQUAL(LINK_ERR_TAG, verify(KEY, LINK_DIR_UPLINK, copy, pkt_len));
  }
}

void test_rejects_wrong_key() {
  TEST_ASSERT_EQUAL(LINK_ERR_TAG, verify(OTHER_KEY, LINK_DIR_UPLINK, pkt, pkt_len));
}

void test_rejects_wrong_direction() {
  TEST_ASSERT_EQUAL(LINK_ERR_TAG, verify(KEY, LINK_DIR_DOWNLINK, pkt, pkt_len));
}

void test_rejects_truncated() {
  TEST_ASSERT_EQUAL(LINK_ERR_TAG, verify(KEY, LINK_DIR_UPLINK, pkt, pkt_len - 1));
  TEST_ASSERT_EQUAL(LINK_ERR_LENGTH, verify(KEY, LINK_DIR_UPLINK, pkt, LINK_OVERHEAD - 1));
}

void test_rejects_replay_and_older_counters() {
  TEST_ASSERT_EQUAL(LINK_OK, verify(KEY, LINK_DIR_UPLINK, pkt, pkt_len));
  TEST_ASSERT_EQUAL(LINK_ERR_REPLAY, verify(KEY, LINK_DIR_UPLINK, pkt, pkt_len));

  uint8_t older[LINK_PACKET_MAX];
  size_t n = link_sign(KEY, LINK_DIR_UPLINK, 0x04, 99, PAYLOAD, sizeof(PAYLOAD), older);
  TEST_ASSERT_EQUAL(LINK_ERR_REPLAY, verify(KEY, LINK_DIR_UPLINK, older, n));
}

void test_accepts_gaps_from_lost_packets() {
  TEST_ASSERT_EQUAL(LINK_OK, verify(KEY, LINK_DIR_UPLINK, pkt, pkt_len));
  uint8_t later[LINK_PACKET_MAX];
  size_t n = link_sign(KEY, LINK_DIR_UPLINK, 0x04, 250, PAYLOAD, sizeof(PAYLOAD), later);
  TEST_ASSERT_EQUAL(LINK_OK, verify(KEY, LINK_DIR_UPLINK, later, n));
}

void test_forged_packet_does_not_advance_replay_state() {
  uint8_t forged[LINK_PACKET_MAX];
  memcpy(forged, pkt, pkt_len);
  forged[1] = 0xff;  // bump the counter without a valid tag
  TEST_ASSERT_EQUAL(LINK_ERR_TAG, verify(KEY, LINK_DIR_UPLINK, forged, pkt_len));
  TEST_ASSERT_FALSE(guard.seen_any);
  TEST_ASSERT_EQUAL(LINK_OK, verify(KEY, LINK_DIR_UPLINK, pkt, pkt_len));
}

void test_persisted_floor_rejects_old_packets() {
  link_replay_init(&guard, 100, true);
  TEST_ASSERT_EQUAL(LINK_ERR_REPLAY, verify(KEY, LINK_DIR_UPLINK, pkt, pkt_len));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_siphash_reference_vectors);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_full_size_packet);
  RUN_TEST(test_rejects_oversize_payload);
  RUN_TEST(test_rejects_every_flipped_bit);
  RUN_TEST(test_rejects_wrong_key);
  RUN_TEST(test_rejects_wrong_direction);
  RUN_TEST(test_rejects_truncated);
  RUN_TEST(test_rejects_replay_and_older_counters);
  RUN_TEST(test_accepts_gaps_from_lost_packets);
  RUN_TEST(test_forged_packet_does_not_advance_replay_state);
  RUN_TEST(test_persisted_floor_rejects_old_packets);
  return UNITY_END();
}
