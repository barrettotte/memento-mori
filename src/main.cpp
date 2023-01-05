#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <limits.h>
#include <memory.h>
#include <NTPClient.h>
#include <RotaryEncoder.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "config.h"
#include "icons.h"

#define ERROR_HALT(s) Serial.println(s); while(1) {}

typedef struct configuration {
    // UTC offset
    // birth
    // death
} configuration;

typedef struct timer {
    unsigned long prevMs;
    unsigned long intervalMs;
} timer;

typedef enum {
    IDLE,          // normal operation
    MENU_IDLE,     // waiting for menu selection
    MENU_UTC,      // set UTC offset
    MENU_BIRTH,    // set birth date
    MENU_DEATH,    // set estimated death date
    MENU_NTP,      // force resync time via NTP
    MENU_EXIT
} state_t;

/* 
   State Machine Summary

   IDLE <---> MENU_IDLE <---+
                            |---> MENU_UTC
                            |---> MENU_BIRTH
                            |---> MENU_DEATH
*/

/*** globals ***/

configuration config;

Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, DISPLAY_RESET); // SDA,SCL
RotaryEncoder encoder(ENCODER_CLK, ENCODER_DT, RotaryEncoder::LatchMode::TWO03);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);
time_t epochTime;

state_t state = IDLE;

bool encoderPressed = false;
unsigned long currMs = 0;

timer encoderTimer = {0, DEBOUNCE_MS};
timer ntpTimer = {0, NTP_INTERVAL_MS};
timer updateDisplayTimer = {0, DISPLAY_INTERVAL_MS};

/*** utilities ***/

void resetScreen() {
    display.clearDisplay();
    display.setCursor(0, 0);
}

uint8_t loadConfig() {
    // TODO:
    return 0;
}

void saveConfig() {
    // TODO:
}

void handleStateTransition() {
    if (state == IDLE) {
        Serial.println("IDLE -> MENU_IDLE");
        state = MENU_IDLE; // IDLE -> MENU_IDLE
        resetScreen();
        display.println("MENU");
        display.display();
    } else if (state == MENU_IDLE) {
        Serial.println("MENU_IDLE -> IDLE");
        state = IDLE; // MENU_IDLE -> IDLE
        resetScreen();
        display.println("IDLE");
        display.display();
    }
}

void resyncNTP() {
    Serial.println("NTP resync.");
    
    timeClient.update();
    epochTime = timeClient.getEpochTime();
    struct tm *t = gmtime((time_t *)&epochTime);

    Serial.printf("%d-%02d-%02d %d:%02d:%02d\n", 
        t->tm_year+1900, t->tm_mon+1, t->tm_mday, 
        t->tm_hour, t->tm_min, t->tm_sec);
}

void drawTime() {
    resetScreen();

    struct tm *t = gmtime((time_t *)&epochTime);
    display.printf("%d-%02d-%02d %d:%02d:%02d", 
        t->tm_year+1900, t->tm_mon+1, t->tm_mday, 
        t->tm_hour, t->tm_min, t->tm_sec);
    display.display();
}

/*** interrupts ***/

IRAM_ATTR void encoderMove() {
    encoder.tick();
}

IRAM_ATTR void encoderPress() {
    encoderPressed = true;
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
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDR)) {
        ERROR_HALT("SSD1306 allocation failed.");
    }
    delay(250);
    resetScreen();
    display.setTextColor(WHITE);
    display.setTextSize(1);
}

void initWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_WIFI_SSID, _WIFI_PASS);

    Serial.printf("Connecting to WiFi [%s]", _WIFI_SSID);
    display.printf("Connecting to WiFi\n[%s] ...", _WIFI_SSID);
    display.display();

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.printf(".");
    }
    Serial.println("IP => " + WiFi.localIP().toString());
    display.clearDisplay();
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

    attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderMove, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_DT), encoderMove, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW), encoderPress, FALLING);

    timeClient.begin();
    timeClient.setTimeOffset(-5 * 3600); // TODO: EST for now (UTC-05:00)

    resetScreen();
    resyncNTP();
    drawTime();

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    currMs = millis();

    if (encoderPressed) {
        Serial.println("Encoder pressed");
        if ((currMs - encoderTimer.prevMs) >= encoderTimer.intervalMs) {
            handleStateTransition();
            encoderTimer.prevMs = currMs;
        }
        encoderPressed = false;
    }
    if ((currMs - ntpTimer.prevMs) >= ntpTimer.intervalMs) {
        resyncNTP();
        ntpTimer.prevMs = currMs;
    }
    if (state == IDLE && (currMs - updateDisplayTimer.prevMs) >= updateDisplayTimer.intervalMs) {
        epochTime += 1; // advance one second for local time
        drawTime();
        updateDisplayTimer.prevMs = currMs;
    }
}
