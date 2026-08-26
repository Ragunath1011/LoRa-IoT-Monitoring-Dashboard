// ============================================================
// SOURCE NODE — ESP8266 + SX1278 (433 MHz)
// Sends numbered packets every 2 seconds.
// Packet format: "PKT:<seq>:SRC"
// Pins: NSS=D8, RST=D0, DIO0=D2
// ============================================================

#include <SPI.h>
#include <LoRa.h>

// Pin definitions
#define LORA_NSS 15   // D8
#define LORA_RST 16   // D0
#define LORA_DIO0 4   // D2

// LoRa parameters
#define LORA_FREQ 433E6
#define LORA_SF 10
#define LORA_BW 125E3
#define LORA_CR 5
#define LORA_TX_POWER 20
#define LORA_PREAMBLE 8

// Timing
#define TX_INTERVAL_MS 2000

// Globals
uint32_t packetSeq = 0;
uint32_t lastTxTime = 0;

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== SOURCE NODE (ESP8266 + SX1278) ===");

  LoRa.setPins(
    LORA_NSS,
    LORA_RST,
    LORA_DIO0
  );

  if (!LoRa.begin(LORA_FREQ)) {

    Serial.println(
      "[ERROR] LoRa init failed! Check wiring."
    );

    while (true) {
      delay(1000);
    }
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TX_POWER);
  LoRa.setPreambleLength(LORA_PREAMBLE);
  LoRa.enableCrc();

  Serial.printf(
    "[OK] LoRa ready | SF%d | BW%.0fkHz | Pwr%ddBm\n",
    LORA_SF,
    LORA_BW / 1000.0,
    LORA_TX_POWER
  );

  Serial.println(
    "----------------------------------------------"
  );
}

void loop() {

  uint32_t now = millis();

  if (now - lastTxTime >= TX_INTERVAL_MS) {

    lastTxTime = now;

    sendPacket();
  }
}

void sendPacket() {

  packetSeq++;

  String payload =
    "PKT:" +
    String(packetSeq) +
    ":SRC";

  LoRa.beginPacket();

  LoRa.print(payload);

  LoRa.endPacket();

  Serial.printf(
    "[TX] Seq=%-5lu Payload='%s'\n",
    packetSeq,
    payload.c_str()
  );
}
