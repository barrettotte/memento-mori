#pragma once

#include "secrets.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_RESET -1 // share Arduino reset pin
#define DISPLAY_I2C_ADDR 0x3c
#define DISPLAY_SDA D3
#define DISPLAY_SCL D4

#define ENCODER_CLK D5
#define ENCODER_DT D6
#define ENCODER_SW D7

#define DEBOUNCE_MS 250
#define NTP_INTERVAL_MS 30000
#define DISPLAY_INTERVAL_MS 1000

#define NTP_SERVER "time.nist.gov"

#define CONFIG_PATH "/config.json"
