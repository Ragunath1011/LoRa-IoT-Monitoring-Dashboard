// ============================================================
// RELAY NODE — ESP32 + SX1278 (433 MHz) + OLED 128x64
//
// LoRa Pins:
// NSS  = GPIO5
// RST  = GPIO14
// DIO0 = GPIO27
//
// OLED:
// SDA = GPIO25
// SCL = GPIO26
// Address = 0x3C
// ============================================================

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- LoRa Pins ----------------

#define LORA_NSS 5
#define LORA_RST 14
#define LORA_DIO0 27

// ---------------- OLED Pins ----------------

#define OLED_SDA 25
#define OLED_SCL 26
#define OLED_ADDR 0x3C

#define SCREEN_W 128
#define SCREEN_H 64

Adafruit_SSD1306 display(
  SCREEN_W,
  SCREEN_H,
  &Wire,
  -1
);

// ---------------- LoRa Parameters ----------------

#define LORA_FREQ 433E6
#define LORA_SF 10
#define LORA_BW 125E3
#define LORA_CR 5
#define LORA_PREAMBLE 8
#define LORA_TX_POWER 20

// ---------------- Timing ----------------

#define TURNAROUND_MS 50

// ---------------- Variables ----------------

uint32_t forwardCount = 0;

uint32_t lastSeq = 0;

int lastRSSI = 0;

float lastSNR = 0.0f;


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println(
    "\n=== RELAY NODE (ESP32 + SX1278) ==="
  );

  // OLED
  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR
      )) {

    Serial.println(
      "[WARN] OLED not found — continuing without display"
    );

  } else {

    showBootScreen();
  }

  // LoRa
  LoRa.setPins(
    LORA_NSS,
    LORA_RST,
    LORA_DIO0
  );

  if (!LoRa.begin(LORA_FREQ)) {

    Serial.println(
      "[ERROR] LoRa init failed!"
    );

    displayError("LoRa FAIL");

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

  updateOLED();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  int packetSize = LoRa.parsePacket();

  if (packetSize > 0) {

    String incoming = "";

    while (LoRa.available()) {

      incoming += (char)LoRa.read();
    }

    // Gateway receives these values
    int rssi = LoRa.packetRssi();

    float snr = LoRa.packetSnr();

    Serial.printf(
      "[RX] '%s' RSSI=%d dBm SNR=%.1f dB\n",
      incoming.c_str(),
      rssi,
      snr
    );

    // Check valid Source packet
    if (
      incoming.startsWith("PKT:") &&
      incoming.endsWith(":SRC")
    ) {

      uint32_t seq =
        extractSeq(incoming);

      lastSeq = seq;

      lastRSSI = rssi;

      lastSNR = snr;

      delay(TURNAROUND_MS);

      forwardPacket(
        seq,
        rssi
      );

      forwardCount++;

      updateOLED();
    }
  }
}


// ============================================================
// EXTRACT PACKET SEQUENCE NUMBER
// ============================================================

uint32_t extractSeq(
  const String &pkt
) {

  int colon1 =
    pkt.indexOf(':');

  int colon2 =
    pkt.indexOf(
      ':',
      colon1 + 1
    );

  if (
    colon1 < 0 ||
    colon2 < 0
  ) {

    return 0;
  }

  return (
    uint32_t
    pkt.substring(
      colon1 + 1,
      colon2
    ).toInt()
  );
}


// ============================================================
// FORWARD PACKET
// ============================================================

void forwardPacket(
  uint32_t seq,
  int rssi
) {

  String fwd =
    "PKT:" +
    String(seq) +
    ":RLY:" +
    String(rssi);

  LoRa.beginPacket();

  LoRa.print(fwd);

  LoRa.endPacket();

  Serial.printf(
    "[TX] Forwarded '%s'\n",
    fwd.c_str()
  );
}


// ============================================================
// OLED BOOT SCREEN
// ============================================================

void showBootScreen() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  display.setCursor(
    10,
    4
  );

  display.println(
    "=== RELAY NODE ==="
  );

  display.setTextSize(2);

  display.setCursor(
    20,
    22
  );

  display.println(
    "ESP32"
  );

  display.setTextSize(1);

  display.setCursor(
    18,
    46
  );

  display.println(
    "SX1278 433 MHz"
  );

  display.display();

  delay(2000);
}


// ============================================================
// OLED ERROR
// ============================================================

void displayError(
  const char *msg
) {

  display.clearDisplay();

  display.setTextSize(2);

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    20
  );

  display.println(msg);

  display.display();
}


// ============================================================
// UPDATE OLED
// ============================================================

void updateOLED() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  // Header
  display.fillRect(
    0,
    0,
    SCREEN_W,
    12,
    SSD1306_WHITE
  );

  display.setTextColor(
    SSD1306_BLACK
  );

  display.setTextSize(1);

  display.setCursor(
    20,
    2
  );

  display.print(
    "[ RELAY NODE ]"
  );

  display.setTextColor(
    SSD1306_WHITE
  );

  // Sequence
  display.setCursor(
    0,
    16
  );

  display.print(
    "Last Seq : "
  );

  display.print(
    lastSeq
  );

  // RSSI
  display.setCursor(
    0,
    28
  );

  display.print(
    "Src RSSI : "
  );

  display.print(
    lastRSSI
  );

  display.print(
    " dBm"
  );

  // SNR
  display.setCursor(
    0,
    40
  );

  display.print(
    "SNR : "
  );

  display.print(
    lastSNR,
    1
  );

  display.print(
    " dB"
  );

  // Forward count
  display.setCursor(
    0,
    52
  );

  display.print(
    "Forwarded : "
  );

  display.print(
    forwardCount
  );

  display.display();
}
