#pragma once

#include "secrets.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_RESET -1 // share Arduino reset pin
#define DISPLAY_I2C_ADDR 0x3c
#define DISPLAY_SDA D2
#define DISPLAY_SCL D1

// note: encoder inputs need to be debounced with 0.1µF caps
#define ENCODER_CLK D5  // A
#define ENCODER_DT  D6  // B
#define ENCODER_SW  D7  // C

#define DEBOUNCE_MS 250          // default debounce input
#define DISPLAY_INTERVAL_MS 1000 // update time display once a second

#define UDP_PORT 8888
#define NTP_WAIT_MS 3000
#define NTP_SYNC_SECS 300
const char* ntpServer = "time.nist.gov";

const char* configPath = "/config.json";
#define UTC_OFFSET_DEFAULT -5.0f // ETC
#define BIRTH_DEFAULT  820515600 // 1996-01-01 12:00:00
#define DEATH_DEFAULT 3345123600 // 2076-01-01 12:00:00
