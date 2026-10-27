#pragma once

#include <link_auth.h>

// Controller side of the authenticated NRF24 link (see lib/LinkAuth).

// Loads the uplink counter from flash and reserves the next block of values.
// Returns false if the counter space is used up (generate a new key).
bool link_begin();

// Signs an uplink packet into `out` (LINK_PACKET_MAX bytes). Returns the
// packet length, or 0 if the payload is too long or the counter is used up.
size_t link_sign_uplink(uint8_t msg_id, const uint8_t *payload, size_t len,
                        uint8_t *out);

// Verifies a downlink packet (telemetry from an ACK payload).
LinkVerifyResult link_verify_downlink(const uint8_t *packet, size_t len,
                                      uint8_t *msg_id,
                                      const uint8_t **payload,
                                      size_t *payload_len);
