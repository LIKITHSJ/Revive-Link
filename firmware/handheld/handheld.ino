#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <mbedtls/gcm.h>
#include "mesh_config.h"

#define BUTTON_PIN   27
#define BLUE_LED      2
#define GREEN_LED    15

TinyGPSPlus gps;
HardwareSerial GPSSerial(2);

RouteEntry routeTable[MAX_NODES];
uint16_t   seqCounter = 0;
uint32_t   lastHeartbeat = 0;

#define SEEN_BUFFER 16
uint16_t seenSeqs[SEEN_BUFFER];
uint8_t  seenSrcs[SEEN_BUFFER];
uint8_t  seenIdx = 0;

bool     encryptPayload(const char* input, MeshPacket& pkt);
bool     decryptPayload(MeshPacket& pkt, char* output);
void     sendPacket(MeshPacket& pkt);
bool     waitForACK(uint16_t seq, uint32_t timeoutMs);
void     handleIncoming();
void     sendSOS();
void     sendHeartbeat();
void     relayPacket(MeshPacket& pkt);
bool     isDuplicate(uint16_t seq, uint8_t src);
uint8_t  getBestNextHop(uint8_t dest);
void     updateRoute(uint8_t dest, uint8_t nextHop, uint8_t hops);
void     sendRREQ(uint8_t dest);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== Node Setup Start ===");

  Serial.println("Step 1: Setting up pins...");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_LED,   OUTPUT);
  pinMode(GREEN_LED,  OUTPUT);
  digitalWrite(BLUE_LED,  LOW);
  digitalWrite(GREEN_LED, LOW);
  Serial.println("Step 1: Pins done");

  Serial.print("Node ID: ");
  Serial.println(THIS_NODE);

  Serial.println("Step 2: Starting GPS Serial...");
  GPSSerial.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("Step 2: GPS Serial done");

  Serial.println("Step 3: Setting LoRa pins...");
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  Serial.println("Step 3: LoRa pins done");

  Serial.println("Step 4: Starting SPI...");
  SPI.begin(18, 19, 23, 5);
  Serial.println("Step 4: SPI done");

  delay(500);

  Serial.println("Step 5: Starting LoRa.begin()...");
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("Step 5: LoRa.begin() FAILED!");
    while (1) {
      digitalWrite(BLUE_LED, !digitalRead(BLUE_LED));
      delay(500);
    }
  }
  Serial.println("Step 5: LoRa.begin() SUCCESS");

  Serial.println("Step 6: Initializing route table...");
  for (int i = 0; i < MAX_NODES; i++) {
    routeTable[i].valid = false;
    Serial.print("  Route ");
    Serial.print(i);
    Serial.println(" initialized");
  }
  Serial.println("Step 6: Route table done");

  Serial.println("Step 7: Updating base route...");
  updateRoute(BASE_ID, BASE_ID, 1);
  Serial.println("Step 7: Base route done");

  Serial.println("Step 8: Starting LoRa receive...");
  LoRa.receive();
  Serial.println("Step 8: LoRa receive done");

  Serial.print("\n✅ Node ");
  Serial.print(THIS_NODE);
  Serial.println(" READY\n");
  Serial.println("=== Node Setup Complete ===\n");
}

void loop() {
  while (GPSSerial.available()) {
    gps.encode(GPSSerial.read());
  }

  handleIncoming();

  if (digitalRead(BUTTON_PIN) == LOW) {
    sendSOS();
    delay(2000);
  }

  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
}

void sendSOS() {
  digitalWrite(BLUE_LED,  HIGH);
  digitalWrite(GREEN_LED, LOW);

  char plaintext[MAX_PAYLOAD];

  if (gps.location.isValid()) {
    snprintf(plaintext, MAX_PAYLOAD, "%d|SOS|%.6f|%.6f",
             THIS_NODE,
             gps.location.lat(),
             gps.location.lng());
  } else {
    snprintf(plaintext, MAX_PAYLOAD, "%d|SOS|NO_FIX", THIS_NODE);
  }

  Serial.print("Sending SOS: ");
  Serial.println(plaintext);

  MeshPacket pkt;
  pkt.src       = THIS_NODE;
  pkt.dst       = BASE_ID;
  pkt.hop_count = 0;
  pkt.seq       = seqCounter++;

  if (!encryptPayload(plaintext, pkt)) {
    Serial.println("Encrypt failed");
    digitalWrite(BLUE_LED, LOW);
    return;
  }

  for (int attempt = 0; attempt < RETRY_COUNT; attempt++) {
    sendPacket(pkt);

    if (waitForACK(pkt.seq, ACK_TIMEOUT)) {
      Serial.println("ACK received — SOS delivered");
      digitalWrite(GREEN_LED, HIGH);
      delay(3000);
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(BLUE_LED,  LOW);
      return;
    }

    Serial.print("No ACK, retry ");
    Serial.println(attempt + 1);

    if (attempt == 1) {
      Serial.println("Sending RREQ...");
      sendRREQ(BASE_ID);
      delay(2000);
    }
  }

  Serial.println("SOS failed after retries");
  for (int i = 0; i < 6; i++) {
    digitalWrite(BLUE_LED, !digitalRead(BLUE_LED));
    delay(200);
  }
  digitalWrite(BLUE_LED, LOW);
}

void sendHeartbeat() {
  char plaintext[MAX_PAYLOAD];
  snprintf(plaintext, MAX_PAYLOAD, "%d|HB", THIS_NODE);

  MeshPacket pkt;
  pkt.src       = THIS_NODE;
  pkt.dst       = BASE_ID;
  pkt.hop_count = 0;
  pkt.seq       = seqCounter++;

  if (!encryptPayload(plaintext, pkt)) return;
  sendPacket(pkt);
  Serial.println("Heartbeat sent");
}

void handleIncoming() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  if (packetSize != PACKET_SIZE) {
    while (LoRa.available()) LoRa.read();
    return;
  }

  MeshPacket pkt;
  uint8_t* buf = (uint8_t*)&pkt;
  for (int i = 0; i < PACKET_SIZE; i++) {
    buf[i] = LoRa.read();
  }

  if (pkt.src == THIS_NODE) return;
  if (isDuplicate(pkt.seq, pkt.src)) return;

  if (pkt.dst == THIS_NODE) {
    char plaintext[MAX_PAYLOAD + 1];
    if (decryptPayload(pkt, plaintext)) {
      Serial.print("Received: ");
      Serial.println(plaintext);
    }
    return;
  }

  if (pkt.dst == BASE_ID && pkt.hop_count < MAX_HOPS) {
    Serial.print("Relaying from node ");
    Serial.println(pkt.src);
    relayPacket(pkt);
    return;
  }

  char plaintext[MAX_PAYLOAD + 1];
  if (decryptPayload(pkt, plaintext)) {
    if (strncmp(plaintext, "RREQ", 4) == 0) {
      uint8_t dest = atoi(plaintext + 5);
      char reply[MAX_PAYLOAD];
      snprintf(reply, MAX_PAYLOAD, "RREP|%d|%d", dest, pkt.hop_count + 1);
      MeshPacket rep;
      rep.src       = THIS_NODE;
      rep.dst       = pkt.src;
      rep.hop_count = 0;
      rep.seq       = seqCounter++;
      if (encryptPayload(reply, rep)) sendPacket(rep);
    } else if (strncmp(plaintext, "RREP", 4) == 0) {
      uint8_t dest = atoi(plaintext + 5);
      uint8_t hops = atoi(strchr(plaintext + 5, '|') + 1);
      updateRoute(dest, pkt.src, hops);
    }
  }
}

void relayPacket(MeshPacket& pkt) {
  pkt.hop_count++;
  sendPacket(pkt);
}

void sendPacket(MeshPacket& pkt) {
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&pkt, PACKET_SIZE);
  LoRa.endPacket();
  LoRa.receive();
}

bool waitForACK(uint16_t seq, uint32_t timeoutMs) {
  uint32_t start = millis();
  Serial.print("\n📡 Waiting for ACK (seq=");
  Serial.print(seq);
  Serial.print(") for ");
  Serial.print(timeoutMs);
  Serial.println("ms\n");

  while (millis() - start < timeoutMs) {
    // Check if any packet is available
    int packetSize = LoRa.parsePacket();
    
    if (packetSize > 0) {
      Serial.print("✓ Got packet, size=");
      Serial.print(packetSize);
      Serial.print(" (expected=");
      Serial.print(PACKET_SIZE);
      Serial.println(")");
      
      if (packetSize != PACKET_SIZE) {
        Serial.println("  ❌ Size mismatch! Draining...");
        while (LoRa.available()) LoRa.read();
        continue;
      }
      
      // Read packet
      MeshPacket pkt;
      uint8_t* buf = (uint8_t*)&pkt;
      for (int i = 0; i < PACKET_SIZE; i++) {
        buf[i] = LoRa.read();
      }

      Serial.print("  Packet details: src=");
      Serial.print(pkt.src);
      Serial.print(" dst=");
      Serial.print(pkt.dst);
      Serial.print(" THIS_NODE=");
      Serial.println(THIS_NODE);

      // Check if packet is for us
      if (pkt.dst != THIS_NODE) {
        Serial.print("  ❌ Not for me (dst=");
        Serial.print(pkt.dst);
        Serial.print(" vs THIS_NODE=");
        Serial.print(THIS_NODE);
        Serial.println("), ignoring");
        continue;
      }

      Serial.println("  ✓ Packet is for me, decrypting...");
      
      // Decrypt
      char plaintext[MAX_PAYLOAD + 1];
      if (!decryptPayload(pkt, plaintext)) {
        Serial.println("  ❌ Decrypt FAILED!");
        continue;
      }

      Serial.print("  ✓ Decrypted: '");
      Serial.print(plaintext);
      Serial.println("'");
      
      // Check if it's an ACK
      char expected[16];
      snprintf(expected, 16, "ACK|%d", seq);
      Serial.print("  Looking for: '");
      Serial.print(expected);
      Serial.println("'");
      
      if (strncmp(plaintext, expected, strlen(expected)) == 0) {
        Serial.println("\n✅✅✅ ACK MATCHED! RETURNING TRUE ✅✅✅\n");
        return true;
      } else {
        Serial.println("  ❌ Message doesn't match expected ACK format");
      }
    }
    
    // Small delay to avoid busy-waiting
    delay(10);
  }
  
  Serial.println("\n❌ ACK TIMEOUT - No valid ACK received\n");
  return false;
}
bool encryptPayload(const char* input, MeshPacket& pkt) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                          AES_KEY, 128) != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  uint8_t nonce[12];
  memcpy(nonce, IV_BASE, 12);
  nonce[10] ^= (pkt.seq >> 8) & 0xFF;
  nonce[11] ^= pkt.seq & 0xFF;

  uint8_t padded[MAX_PAYLOAD];
  memset(padded, 0, MAX_PAYLOAD);
  size_t inputLen = strlen(input);
  if (inputLen > MAX_PAYLOAD) inputLen = MAX_PAYLOAD;
  memcpy(padded, input, inputLen);

  memset(pkt.payload, 0, MAX_PAYLOAD);

  int ret = mbedtls_gcm_crypt_and_tag(
    &gcm,
    MBEDTLS_GCM_ENCRYPT,
    MAX_PAYLOAD,
    nonce, 12,
    NULL, 0,
    padded,
    pkt.payload,
    TAG_LEN, pkt.tag
  );

  mbedtls_gcm_free(&gcm);
  return (ret == 0);
}

bool decryptPayload(MeshPacket& pkt, char* output) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);

  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                          AES_KEY, 128) != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  uint8_t nonce[12];
  memcpy(nonce, IV_BASE, 12);
  nonce[10] ^= (pkt.seq >> 8) & 0xFF;
  nonce[11] ^= pkt.seq & 0xFF;

  uint8_t decrypted[MAX_PAYLOAD];
  memset(decrypted, 0, MAX_PAYLOAD);

  int ret = mbedtls_gcm_auth_decrypt(
    &gcm,
    MAX_PAYLOAD,
    nonce, 12,
    NULL, 0,
    pkt.tag, TAG_LEN,
    pkt.payload,
    decrypted
  );

  mbedtls_gcm_free(&gcm);
  if (ret != 0) return false;

  memcpy(output, decrypted, MAX_PAYLOAD);
  output[MAX_PAYLOAD] = '\0';
  return true;
}

uint8_t getBestNextHop(uint8_t dest) {
  if (routeTable[dest].valid &&
      millis() - routeTable[dest].last_seen < ROUTE_TIMEOUT) {
    return routeTable[dest].next_hop;
  }
  return dest;
}

void updateRoute(uint8_t dest, uint8_t nextHop, uint8_t hops) {
  if (dest >= MAX_NODES) return;
  routeTable[dest].dest      = dest;
  routeTable[dest].next_hop  = nextHop;
  routeTable[dest].hop_count = hops;
  routeTable[dest].last_seen = millis();
  routeTable[dest].valid     = true;
}

void sendRREQ(uint8_t dest) {
  char plaintext[MAX_PAYLOAD];
  snprintf(plaintext, MAX_PAYLOAD, "RREQ|%d", dest);

  MeshPacket pkt;
  pkt.src       = THIS_NODE;
  pkt.dst       = 0xFF;
  pkt.hop_count = 0;
  pkt.seq       = seqCounter++;

  if (encryptPayload(plaintext, pkt)) sendPacket(pkt);
}

bool isDuplicate(uint16_t seq, uint8_t src) {
  for (int i = 0; i < SEEN_BUFFER; i++) {
    if (seenSeqs[i] == seq && seenSrcs[i] == src) return true;
  }
  seenSeqs[seenIdx] = seq;
  seenSrcs[seenIdx] = src;
  seenIdx = (seenIdx + 1) % SEEN_BUFFER;
  return false;
}