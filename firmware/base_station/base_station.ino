#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <mbedtls/gcm.h>
#include "mesh_config.h"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define RED_LED   2

RouteEntry routeTable[MAX_NODES];
uint16_t   seqCounter = 0;

#define SEEN_BUFFER 16
uint16_t seenSeqs[SEEN_BUFFER];
uint8_t  seenSrcs[SEEN_BUFFER];
uint8_t  seenIdx = 0;

bool    encryptPayload(const char* input, MeshPacket& pkt);
bool    decryptPayload(MeshPacket& pkt, char* output);
void    sendPacket(MeshPacket& pkt);
void    sendACK(uint8_t dst, uint16_t seq);
void    handleIncoming();
void    handleSOS(uint8_t src, const char* plaintext);
void    handleHeartbeat(uint8_t src);
void    handleRREQ(MeshPacket& pkt);
bool    isDuplicate(uint16_t seq, uint8_t src);
void    updateRoute(uint8_t dest, uint8_t nextHop, uint8_t hops);
void    showDisplay(const char* line1, const char* line2, const char* line3);
void    showAlert(const char* nodeStr, const char* gpsStr);

void setup() {
  Serial.begin(115200);

  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Base Station");
    display.println("Initializing...");
    display.display();
  }

  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  SPI.begin(18, 19, 23, 5);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed!");
    showDisplay("LoRa FAILED", "Check wiring", "");
    while (1);
  }

  for (int i = 0; i < MAX_NODES; i++) {
    routeTable[i].valid = false;
  }

  LoRa.receive();
  Serial.println("Base Station Ready");
  showDisplay("Base Station", "Ready", "Waiting...");
}

void loop() {
  handleIncoming();
}

void handleIncoming() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  Serial.print("Packet size received: ");
  Serial.println(packetSize);
  Serial.print("Expected PACKET_SIZE: ");
  Serial.println(PACKET_SIZE);

  if (packetSize != PACKET_SIZE) {
    Serial.println("Size mismatch — draining");
    while (LoRa.available()) LoRa.read();
    return;
  }

  MeshPacket pkt;
  uint8_t* buf = (uint8_t*)&pkt;
  for (int i = 0; i < PACKET_SIZE; i++) {
    buf[i] = LoRa.read();
  }

  Serial.print("src="); Serial.print(pkt.src);
  Serial.print(" dst="); Serial.print(pkt.dst);
  Serial.print(" seq="); Serial.print(pkt.seq);
  Serial.print(" hop="); Serial.println(pkt.hop_count);

  if (pkt.src == BASE_ID) return;

  if (isDuplicate(pkt.seq, pkt.src)) {
    Serial.println("Duplicate, dropping");
    return;
  }

  if (pkt.dst != BASE_ID) return;

  char plaintext[MAX_PAYLOAD + 1];
  if (!decryptPayload(pkt, plaintext)) {
    Serial.println("Decrypt failed — bad key or tampered packet");
    return;
  }

  Serial.print("Decrypted: ");
  Serial.println(plaintext);

  updateRoute(pkt.src, pkt.src, pkt.hop_count + 1);

  if (strstr(plaintext, "|SOS|") != NULL) {
    handleSOS(pkt.src, plaintext);
    sendACK(pkt.src, pkt.seq);
  } else if (strstr(plaintext, "|HB") != NULL) {
    handleHeartbeat(pkt.src);
    sendACK(pkt.src, pkt.seq);
  } else if (strncmp(plaintext, "RREQ", 4) == 0) {
    handleRREQ(pkt);
  } else {
    Serial.println("Unknown message type");
  }
}

void handleSOS(uint8_t src, const char* plaintext) {
  digitalWrite(RED_LED, HIGH);

  char buf[MAX_PAYLOAD + 1];
  strncpy(buf, plaintext, MAX_PAYLOAD);

  char nodeStr[8], gpsStr[48];
  snprintf(nodeStr, sizeof(nodeStr), "NODE %d", src);

  char* sosPtr = strstr(buf, "SOS|");
  if (sosPtr) {
    strncpy(gpsStr, sosPtr + 4, 47);
    gpsStr[47] = '\0';
  } else {
    strcpy(gpsStr, "NO_FIX");
  }

  Serial.print("*** SOS from Node ");
  Serial.print(src);
  Serial.print(" | GPS: ");
  Serial.println(gpsStr);

  showAlert(nodeStr, gpsStr);

  delay(5000);
  digitalWrite(RED_LED, LOW);
  showDisplay("Base Station", "Ready", "Waiting...");
}

void handleHeartbeat(uint8_t src) {
  char line1[20];
  snprintf(line1, sizeof(line1), "Node %d alive", src);
  Serial.println(line1);
  showDisplay("Heartbeat", line1, "");
  delay(2000);
  showDisplay("Base Station", "Ready", "Waiting...");
}

void handleRREQ(MeshPacket& pkt) {
  char plaintext[MAX_PAYLOAD + 1];
  if (!decryptPayload(pkt, plaintext)) return;

  uint8_t dest = atoi(plaintext + 5);

  char reply[MAX_PAYLOAD];
  snprintf(reply, MAX_PAYLOAD, "RREP|%d|1", dest);

  MeshPacket rep;
  rep.src       = BASE_ID;
  rep.dst       = pkt.src;
  rep.hop_count = 0;
  rep.seq       = seqCounter++;

  if (encryptPayload(reply, rep)) {
    sendPacket(rep);
    Serial.print("RREP sent to node ");
    Serial.println(pkt.src);
  }
}

void sendACK(uint8_t dst, uint16_t seq) {
  char ackMsg[MAX_PAYLOAD];
  snprintf(ackMsg, MAX_PAYLOAD, "ACK|%d", seq);

  MeshPacket pkt;
  pkt.src       = BASE_ID;
  pkt.dst       = dst;
  pkt.hop_count = 0;
  pkt.seq       = seqCounter++;

  if (encryptPayload(ackMsg, pkt)) {
    sendPacket(pkt);
    Serial.print("ACK sent to node ");
    Serial.println(dst);
  }
}

void sendPacket(MeshPacket& pkt) {
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&pkt, PACKET_SIZE);
  LoRa.endPacket();
  LoRa.receive();
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

void showDisplay(const char* line1, const char* line2, const char* line3) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.setCursor(0, 20);
  display.println(line2);
  display.setCursor(0, 40);
  display.println(line3);
  display.display();
}

void showAlert(const char* nodeStr, const char* gpsStr) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("!! SOS !!");
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(nodeStr);
  display.setCursor(0, 35);
  display.println("GPS:");
  display.setCursor(0, 47);
  display.println(gpsStr);
  display.display();
}

void updateRoute(uint8_t dest, uint8_t nextHop, uint8_t hops) {
  if (dest >= MAX_NODES) return;
  routeTable[dest].dest      = dest;
  routeTable[dest].next_hop  = nextHop;
  routeTable[dest].hop_count = hops;
  routeTable[dest].last_seen = millis();
  routeTable[dest].valid     = true;
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