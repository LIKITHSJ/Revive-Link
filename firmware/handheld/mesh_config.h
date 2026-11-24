#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

#include <Arduino.h>

// ─── Node IDs ───────────────────────────────────────────
#define BASE_ID   0
#define NODE_1    1
#define NODE_2    2
#define NODE_3    3

#define THIS_NODE NODE_3  // NODE_1/2/3 for handhelds, BASE_ID for base

// ─── LoRa pins ──────────────────────────────────────────
#define LORA_CS   5
#define LORA_RST  12
#define LORA_IRQ  13
#define LORA_FREQ 433E6

// ─── AES-128-GCM key ────────────────────────────────────
// The key and IV live in secrets.h (kept private, never uploaded).
// See secrets.example.h for the format.
#include "secrets.h"

// ─── Packet structure ───────────────────────────────────
#define MAX_PAYLOAD   64
#define MAX_HOPS       5
#define TAG_LEN       16

typedef struct __attribute__((packed)) {
  uint8_t  src;
  uint8_t  dst;
  uint8_t  hop_count;
  uint16_t seq;
  uint8_t  payload[MAX_PAYLOAD];
  uint8_t  tag[TAG_LEN];
} MeshPacket;

#define PACKET_SIZE sizeof(MeshPacket)

// ─── AODV routing table ─────────────────────────────────
#define MAX_NODES     4
#define ROUTE_TIMEOUT 30000

typedef struct {
  uint8_t  dest;
  uint8_t  next_hop;
  uint8_t  hop_count;
  uint32_t last_seen;
  bool     valid;
} RouteEntry;

// ─── Timing ──────────────────────────────────────────────
#define ACK_TIMEOUT        5000
#define RETRY_COUNT           3
#define HEARTBEAT_INTERVAL 30000

// ─── Message types ──────────────────────────────────────
#define MSG_SOS       0x01
#define MSG_ACK       0x02
#define MSG_RELAY     0x03
#define MSG_HEARTBEAT 0x04
#define MSG_RREQ      0x05
#define MSG_RREP      0x06

#endif