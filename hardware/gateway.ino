// ============================================================
// GATEWAY NODE — ESP8266 + SX1278 (433 MHz) + OLED 128x64
//
// Board: Generic ESP8266 Module
//
// LoRa:
// NSS  = GPIO15 (D8)
// RST  = GPIO16 (D0)
// DIO0 = GPIO4  (D2)
//
// OLED:
// SDA = GPIO0 (D3)
// SCL = GPIO2 (D4)
// ============================================================

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


// ============================================================
// PIN DEFINITIONS
// ============================================================

#define LORA_NSS 15
#define LORA_RST 16
#define LORA_DIO0 4

#define OLED_SDA 0
#define OLED_SCL 2

#define OLED_ADDR 0x3C

#define SCREEN_W 128
#define SCREEN_H 64


Adafruit_SSD1306 display(
  SCREEN_W,
  SCREEN_H,
  &Wire,
  -1
);


// ============================================================
// LORA PARAMETERS
// Must match Source and Relay
// ============================================================

#define LORA_FREQ 433E6

#define LORA_SF 10

#define LORA_BW 125E3

#define LORA_CR 5

#define LORA_PREAMBLE 8


// ============================================================
// RELAY TIMEOUT
// ============================================================

#define RELAY_TIMEOUT_MS 10000UL


// ============================================================
// RSSI SMOOTHING
// ============================================================

#define SMOOTH_N 5


// ============================================================
// PACKET RECORD
// ============================================================

struct SeqRecord {

  uint32_t seq;

  int directRSSI;

  int relayRSSI;

  int relayAtSrc;

  bool hasDirect;

  bool hasRelay;
};


#define RECORD_SLOTS 8

SeqRecord records[
  RECORD_SLOTS
];

int recordHead = 0;


// ============================================================
// DISPLAY STATE
// ============================================================

int dispDirectRSSI = 0;

int dispRelayRSSI = 0;

int dispRelayAtSrc = 0;

int dispGain = 0;

uint32_t dispSeq = 0;

uint32_t totalDirect = 0;

uint32_t totalRelay = 0;


// ============================================================
// RELAY STATUS
// ============================================================

bool relayAlive = false;

uint32_t lastRelayPacketMs = 0;


// ============================================================
// RSSI SMOOTHING BUFFERS
// ============================================================

int directBuf[
  SMOOTH_N
] = {0};

int relayBuf[
  SMOOTH_N
] = {0};

int bufIdx = 0;

bool bufFull = false;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void processPacket(
  const String &pkt,
  int gwRSSI,
  float snr
);

void computeAndDisplay(
  SeqRecord *r
);

void checkRelayTimeout();

void resetRelayState();

SeqRecord* getOrCreateRecord(
  uint32_t seq
);

uint32_t extractSeq(
  const String &pkt
);

int extractRelayRSSI(
  const String &pkt
);

void smoothDirect(
  int v
);

void smoothRelay(
  int v
);

int smoothed(
  int *buf
);

void showBootScreen();

void displayError(
  const char *msg
);

void updateOLED();


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println(
    "\n=== GATEWAY NODE (ESP8266 + SX1278) ==="
  );


  // ---------------- OLED ----------------

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDR
    )
  ) {

    Serial.println(
      "[WARN] OLED not found"
    );

  } else {

    showBootScreen();
  }


  // ---------------- LoRa ----------------

  LoRa.setPins(
    LORA_NSS,
    LORA_RST,
    LORA_DIO0
  );

  if (!LoRa.begin(LORA_FREQ)) {

    Serial.println(
      "[ERROR] LoRa init failed!"
    );

    displayError(
      "LoRa FAIL"
    );

    while (true) {
      delay(1000);
    }
  }


  LoRa.setSpreadingFactor(
    LORA_SF
  );

  LoRa.setSignalBandwidth(
    LORA_BW
  );

  LoRa.setCodingRate4(
    LORA_CR
  );

  LoRa.setPreambleLength(
    LORA_PREAMBLE
  );

  LoRa.enableCrc();


  Serial.printf(
    "[OK] LoRa ready | SF%d | BW%.0fkHz\n",
    LORA_SF,
    LORA_BW / 1000.0
  );


  Serial.println(
    "Seq | Type | GW RSSI | RelayRSSI | Gain"
  );

  Serial.println(
    "-----|--------|---------|-----------|-----"
  );


  memset(
    records,
    0,
    sizeof(records)
  );


  lastRelayPacketMs =
    millis();


  updateOLED();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  checkRelayTimeout();


  int pktSize =
    LoRa.parsePacket();


  if (pktSize > 0) {

    String pkt = "";


    while (
      LoRa.available()
    ) {

      pkt += (
        char
      )LoRa.read();
    }


    int gwRSSI =
      LoRa.packetRssi();


    float gwSNR =
      LoRa.packetSnr();


    processPacket(
      pkt,
      gwRSSI,
      gwSNR
    );
  }
}


// ============================================================
// RELAY TIMEOUT CHECK
// ============================================================

void checkRelayTimeout() {

  if (relayAlive) {

    uint32_t elapsed =
      millis() -
      lastRelayPacketMs;


    if (
      elapsed >=
      RELAY_TIMEOUT_MS
    ) {

      Serial.println(
        "[INFO] Relay timeout — marking relay OFF"
      );

      resetRelayState();

      updateOLED();
    }
  }
}


// ============================================================
// RESET RELAY STATE
// ============================================================

void resetRelayState() {

  relayAlive = false;

  dispRelayRSSI = 0;

  dispRelayAtSrc = 0;

  dispGain = 0;

  totalRelay = 0;


  memset(
    relayBuf,
    0,
    sizeof(relayBuf)
  );


  for (
    int i = 0;
    i < RECORD_SLOTS;
    i++
  ) {

    records[i].hasRelay = false;

    records[i].relayRSSI = 0;

    records[i].relayAtSrc = 0;
  }
}


// ============================================================
// PROCESS RECEIVED PACKET
// ============================================================

void processPacket(
  const String &pkt,
  int gwRSSI,
  float snr
) {


  // ==========================================================
  // DIRECT PACKET
  //
  // Format:
  // PKT:<seq>:SRC
  // ==========================================================

  if (
    pkt.startsWith("PKT:") &&
    pkt.endsWith(":SRC")
  ) {

    uint32_t seq =
      extractSeq(pkt);


    totalDirect++;


    SeqRecord *r =
      getOrCreateRecord(seq);


    r->directRSSI =
      gwRSSI;

    r->hasDirect =
      true;


    Serial.printf(
      "%-4lu | DIRECT | %4d dBm | | SNR=%.1fdB\n",
      seq,
      gwRSSI,
      snr
    );


    smoothDirect(
      gwRSSI
    );


    dispSeq =
      seq;


    dispDirectRSSI =
      smoothed(
        directBuf
      );


    if (
      relayAlive &&
      r->hasRelay
    ) {

      computeAndDisplay(r);

    } else {

      updateOLED();
    }


    return;
  }


  // ==========================================================
  // RELAY PACKET
  //
  // Format:
  // PKT:<seq>:RLY:<rssiAtRelay>
  // ==========================================================

  if (
    pkt.startsWith("PKT:") &&
    pkt.indexOf(":RLY:") > 0
  ) {

    uint32_t seq =
      extractSeq(pkt);


    int relayAtSrc =
      extractRelayRSSI(pkt);


    totalRelay++;


    // Relay is alive
    relayAlive = true;

    lastRelayPacketMs =
      millis();


    SeqRecord *r =
      getOrCreateRecord(seq);


    r->relayRSSI =
      gwRSSI;

    r->relayAtSrc =
      relayAtSrc;

    r->hasRelay =
      true;


    smoothRelay(
      gwRSSI
    );


    Serial.printf(
      "%-4lu | RELAY | %4d dBm | %4d dBm |",
      seq,
      gwRSSI,
      relayAtSrc
    );


    if (
      r->hasDirect
    ) {

      int gain =
        gwRSSI -
        r->directRSSI;


      Serial.printf(
        " %+d dB",
        gain
      );


      computeAndDisplay(r);

    } else {

      Serial.print(
        " n/a"
      );


      dispSeq =
        seq;


      dispRelayRSSI =
        smoothed(
          relayBuf
        );


      dispRelayAtSrc =
        relayAtSrc;


      updateOLED();
    }


    Serial.printf(
      " SNR=%.1fdB\n",
      snr
    );


    return;
  }


  // ==========================================================
  // UNKNOWN PACKET
  // ==========================================================

  Serial.printf(
    "[UNKNOWN] '%s' RSSI=%d\n",
    pkt.c_str(),
    gwRSSI
  );
}


// ============================================================
// CALCULATE DISPLAY VALUES
// ============================================================

void computeAndDisplay(
  SeqRecord *r
) {

  dispSeq =
    r->seq;


  dispDirectRSSI =
    smoothed(
      directBuf
    );


  dispRelayRSSI =
    smoothed(
      relayBuf
    );


  dispRelayAtSrc =
    r->relayAtSrc;


  dispGain =
    dispRelayRSSI -
    dispDirectRSSI;


  updateOLED();
}


// ============================================================
// FIND OR CREATE PACKET RECORD
// ============================================================

SeqRecord* getOrCreateRecord(
  uint32_t seq
) {

  for (
    int i = 0;
    i < RECORD_SLOTS;
    i++
  ) {

    if (
      records[i].seq ==
      seq
    ) {

      return &records[i];
    }
  }


  SeqRecord *r =
    &records[
      recordHead %
      RECORD_SLOTS
    ];


  recordHead++;


  memset(
    r,
    0,
    sizeof(SeqRecord)
  );


  r->seq =
    seq;


  return r;
}


// ============================================================
// EXTRACT SEQUENCE NUMBER
// ============================================================

uint32_t extractSeq(
  const String &pkt
) {

  int c1 =
    pkt.indexOf(':');


  int c2 =
    pkt.indexOf(
      ':',
      c1 + 1
    );


  if (
    c1 < 0 ||
    c2 < 0
  ) {

    return 0;
  }


  return (
    uint32_t
    pkt.substring(
      c1 + 1,
      c2
    ).toInt()
  );
}


// ============================================================
// EXTRACT RELAY RSSI
// ============================================================

int extractRelayRSSI(
  const String &pkt
) {

  int rlyIdx =
    pkt.indexOf(
      ":RLY:"
    );


  if (
    rlyIdx < 0
  ) {

    return 0;
  }


  return pkt.substring(
    rlyIdx + 5
  ).toInt();
}


// ============================================================
// RSSI SMOOTHING
// ============================================================

void smoothDirect(
  int v
) {

  directBuf[
    bufIdx %
    SMOOTH_N
  ] = v;


  bufIdx++;


  if (
    bufIdx >=
    SMOOTH_N
  ) {

    bufFull = true;
  }
}


void smoothRelay(
  int v
) {

  relayBuf[
    bufIdx %
    SMOOTH_N
  ] = v;
}


int smoothed(
  int *buf
) {

  int n =
    bufFull ?
    SMOOTH_N :
    bufIdx;


  if (
    n == 0
  ) {

    return 0;
  }


  long sum = 0;


  for (
    int i = 0;
    i < n &&
    i < SMOOTH_N;
    i++
  ) {

    sum += buf[i];
  }


  return (
    int
  )(sum / n);
}


// ============================================================
// UPDATE OLED
// ============================================================

void updateOLED() {

  display.clearDisplay();


  // Header
  display.fillRect(
    0,
    0,
    SCREEN_W,
    10,
    SSD1306_WHITE
  );


  display.setTextColor(
    SSD1306_BLACK
  );


  display.setTextSize(1);


  display.setCursor(
    22,
    1
  );


  display.print(
    "[ GATEWAY NODE ]"
  );


  display.setTextColor(
    SSD1306_WHITE
  );


  // Sequence and counts
  display.setCursor(
    0,
    11
  );


  display.print(
    "S:"
  );

  display.print(
    dispSeq
  );


  display.print(
    " D:"
  );

  display.print(
    totalDirect
  );


  display.print(
    " R:"
  );

  display.print(
    totalRelay
  );


  // Divider
  display.drawFastHLine(
    0,
    20,
    SCREEN_W,
    SSD1306_WHITE
  );


  // Direct RSSI
  display.setCursor(
    0,
    22
  );


  display.print(
    "Dir :"
  );

  display.print(
    dispDirectRSSI
  );

  display.print(
    " dBm"
  );


  // Relay RSSI
  display.setCursor(
    0,
    32
  );


  display.print(
    "Rly :"
  );


  if (
    relayAlive
  ) {

    display.print(
      dispRelayRSSI
    );

    display.print(
      " dBm"
    );

  } else {

    display.print(
      "--- (OFF)"
    );
  }


  // RSSI Gain
  display.setCursor(
    0,
    42
  );


  display.print(
    "Gain:"
  );


  if (
    relayAlive &&
    dispGain != 0
  ) {

    if (
      dispGain > 0
    ) {

      display.print(
        "+"
      );
    }


    display.print(
      dispGain
    );


    display.print(
      " dB"
    );

  } else {

    display.print(
      "--- dB"
    );
  }


  // Divider
  display.drawFastHLine(
    0,
    53,
    SCREEN_W,
    SSD1306_WHITE
  );


  // Relay status
  display.setCursor(
    0,
    55
  );


  if (
    !relayAlive
  ) {

    display.print(
      "RELAY OFF|Direct only"
    );

  } else if (
    dispGain > 5
  ) {

    display.print(
      "RELAY ON|+"
    );

    display.print(
      dispGain
    );

    display.print(
      "dB GAIN"
    );

  } else {

    display.print(
      "RELAY ON|Monitoring"
    );
  }


  display.display();
}


// ============================================================
// BOOT SCREEN
// ============================================================

void showBootScreen() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  display.setCursor(
    8,
    4
  );

  display.println(
    "=== GATEWAY NODE ==="
  );

  display.setTextSize(2);

  display.setCursor(
    15,
    22
  );

  display.println(
    "ESP8266"
  );

  display.setTextSize(1);

  display.setCursor(
    18,
    50
  );

  display.println(
    "SX1278 433 MHz"
  );

  display.display();

  delay(2000);
}


// ============================================================
// DISPLAY ERROR
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

  display.println(
    msg
  );

  display.display();
}
