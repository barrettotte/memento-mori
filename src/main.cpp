#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <memory.h>
#include <SSD1306Wire.h>

#include "config.h"
#include "icons.h"

#define ERROR_HALT(s) Serial.println(s); while(1) {}

typedef struct {
    // timezone
    // birth
    // death
} config_t;

/*** globals ***/

auto config = std::unique_ptr<config_t>(new config_t);
SSD1306Wire display(DSPLY_I2C_ADDR, DSPLY_SDA, DSPLY_SCL);

/*** utilities ***/

uint8_t loadConfig() {
    // TODO:
}

void saveConfig() {
    // TODO:
}

/*** initialization ***/

void initSerial() {
    Serial.begin(9600);
    Serial.println();

    for(uint8_t t = 3; t > 0; t--){
        Serial.printf("WAIT %d...\n", t);
        Serial.flush();
        delay(500);
    }
    Serial.println("\n* * * START * * *");
}

void initDisplay() {
    display.init();
    display.clear();
    display.display();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);
    display.setFont(ArialMT_Plain_10);
}

void initWifi() {
    // TODO: display progress to OLED
    // TODO: use WifiManager to avoid hardcoded creds
}

void initFs() {
    if (!LittleFS.begin()) {
        ERROR_HALT("Error occurred while mounting LittleFS.");
    }
    delay(50);
}

void initConfig() {
    // TODO: read from file system
}

/*** main ***/

void setup() {
    initSerial();
    initDisplay();
    initWifi();
    initFs();
    initConfig();
    delay(100);

    // TODO: init NTP client

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
}
