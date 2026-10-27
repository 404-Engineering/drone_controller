#pragma once

// Radio message payloads (see Following-Drone docs/DESIGN.md section 7).
// These are the payload part of the authenticated envelope in lib/LinkAuth;
// msg_id and counter live in the envelope header. Both ends are RP2350
// (little-endian), so the packed structs are sent as-is.
//
// Shared by drone_controller and nrf_module: keep both copies identical.

#include <stdint.h>

#include <link_auth.h>

// Bump when a payload layout changes; sent in TELEM_STATUS.
#define PROTOCOL_VERSION 1

enum MsgId : uint8_t {
  MSG_BEACON       = 0x01,
  MSG_COMMAND      = 0x02,
  MSG_TELEM_STATUS = 0x03,
  MSG_CONTROL      = 0x04,
};

enum ArmState : uint8_t {
  ARM_DISARMED = 0,
  ARM_ARMED    = 1,
};

// Sent at 50 Hz. Arm and kill are state, not one-shot commands: every packet
// repeats them, so a lost packet is covered by the next one. Kill latches on
// the drone until a disarm / re-arm.
struct __attribute__((packed)) ControlPayload {
  int16_t roll;       // -1000..1000
  int16_t pitch;      // -1000..1000
  int16_t yaw;        // -1000..1000
  uint16_t throttle;  // 0..1000
  uint8_t mode;       // flight mode, TBD
  uint8_t arm;        // ArmState
  uint8_t kill;       // 1 = stop motors now
  uint8_t flags;      // reserved, 0
};

static_assert(sizeof(ControlPayload) <= LINK_MAX_PAYLOAD,
              "ControlPayload doesn't fit in one radio packet");

// Failsafe stages on the drone, from how long since the last good uplink.
enum LinkFailsafe : uint8_t {
  FAILSAFE_NONE  = 0,  // link fine
  FAILSAFE_HOLD  = 1,  // > 200 ms: hold position
  FAILSAFE_HOVER = 2,  // > 1 s: hover
  FAILSAFE_RTH   = 3,  // > 5 s: return home and land
};

// Drone -> controller in every ACK payload. One exchange behind: the drone
// loads it before the next uplink packet arrives.
struct __attribute__((packed)) TelemStatusPayload {
  uint32_t rx_counter;      // last accepted uplink counter
  uint8_t version;          // PROTOCOL_VERSION
  uint8_t link_quality;     // % of expected uplink packets accepted, last 1 s
  uint8_t failsafe;         // LinkFailsafe
  uint8_t arm;              // ArmState as the drone sees it
  uint8_t kill_latched;     // 1 until a disarm with kill clear
  uint8_t flight_state;     // TBD, 0 for now
  uint16_t battery_mv;      // 0 = unknown
};

static_assert(sizeof(TelemStatusPayload) <= LINK_MAX_PAYLOAD,
              "TelemStatusPayload doesn't fit in one radio packet");
