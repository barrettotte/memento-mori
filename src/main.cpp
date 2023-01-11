#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <limits.h>
#include <memory.h>
#include <NTPClient.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "config.h"
#include "hourglass.h"

#define errorHalt(s) Serial.println(s); while(1) {}

enum state {
    STATE_IDLE_YEAR,  // show year percentage
    STATE_IDLE_LIFE,  // show life percentage
    STATE_SHOW_UTC,   // display UTC offset setting
    STATE_SHOW_BIRTH, // display current birth setting
    STATE_SHOW_DEATH, // display current death setting
    STATE_SHOW_NTP,   // display NTP refresh screen
    STATE_SET_UTC,    // set UTC offset
    STATE_SET_BIRTH,  // set birth date
    STATE_SET_DEATH,  // set estimated death date
    STATE_SET_NTP,    // force resync time via NTP
};

enum direction {
    CW = 1,
    CCW = -1
};

struct timer {
    unsigned long prevMs;
    unsigned long intervalMs;
};

struct range {
    int min;
    int max;
};

struct configuration {
    // UTC offset
    // birth
    // death
};

struct rotaryEncoder {
    int prevClk;
    int currClk;
    direction dir;
    timer btn;
    bool moved;
    bool pressed;
};

/*** globals ***/

configuration config;
rotaryEncoder encoder;

Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, DISPLAY_RESET); // SDA,SCL

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);
time_t epochTime;

state prevState = STATE_IDLE_LIFE;
state currState = STATE_IDLE_YEAR;
uint8_t hourglassIdx = 0;

range pageRange;
timer ntpTimer;
timer updateDisplayTimer;

unsigned long currMs = 0;

/*** utilities ***/

void resetScreen() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextColor(WHITE);
    display.setTextSize(1);
}

void drawCenteredText(String text, bool horizontal, bool vertical) {
    int16_t x, y;
    uint16_t w, h;

    display.getTextBounds(text, 0, 0, &x, &y, &w, &h);
    w = horizontal ? (DISPLAY_WIDTH - w) / 2 : display.getCursorX();
    h = vertical ? (DISPLAY_HEIGHT - h) / 2 : display.getCursorY();

    display.setCursor(w, h);
    display.println(text);
}

void drawTime() {
    struct tm *t = gmtime((time_t *) &epochTime);
    char buffer[32];

    sprintf(buffer, "%04u-%02u-%02u %02u:%02u:%02u", 
        (uint8_t) t->tm_year+1900, (uint8_t) t->tm_mon+1, (uint8_t) t->tm_mday, 
        (uint8_t) t->tm_hour, (uint8_t) t->tm_min, (uint8_t) t->tm_sec);

    drawCenteredText(buffer, true, true);
}

void drawPage() {
    resetScreen();

    switch (currState) {
        case STATE_IDLE_YEAR:
            drawTime();
            break;
        case STATE_IDLE_LIFE:
            // drawCenteredText("Life Progress", false, true);
            display.drawBitmap(
                (DISPLAY_WIDTH / 2) - (HOURGLASS_WIDTH / 2),
                (DISPLAY_HEIGHT / 2) - (HOURGLASS_HEIGHT / 2),
                hourglassFrames[hourglassIdx++], HOURGLASS_WIDTH, HOURGLASS_HEIGHT, 1);

            if (hourglassIdx >= HOURGLASS_FRAMES) {
                hourglassIdx = 0;
            }
            break;
        case STATE_SHOW_UTC:
            drawCenteredText("UTC Offset", true, true);
            break;
        case STATE_SHOW_BIRTH:
            drawCenteredText("Birth Date", true, true);
            break;
        case STATE_SHOW_DEATH:
            drawCenteredText("Estimated", true, true);
            display.display();
            display.setCursor(0, display.getCursorY()+1);
            drawCenteredText("Death Date", true, false);
            break;
        case STATE_SHOW_NTP:
            drawCenteredText("NTP Refresh", true, true);
            break;
        default:
            Serial.printf("Warning: Unnecessary page draw for state %d\n", currState);
            break;
    }
    display.display();
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

uint8_t loadConfig() {
    // TODO:
    return 0;
}

void saveConfig() {
    // TODO:
}

void handleEncoderMove() {
    if (digitalRead(ENCODER_DT) != encoder.currClk) {
        encoder.dir = CCW;
    } else {
        encoder.dir = CW;
    }
    int nextState = (int) currState;
    nextState += encoder.dir;

    // wraparound states
    if (nextState < pageRange.min) {
        nextState = pageRange.max;
    } else if (nextState > pageRange.max) {
        nextState = pageRange.min;
    }
    prevState = currState;
    currState = (state) nextState;

    drawPage();
}

void handleEncoderPress() {
    // TODO: settings
}

/*** interrupts ***/

IRAM_ATTR void encoderMove() {
    encoder.currClk = digitalRead(ENCODER_CLK);

    if (encoder.currClk != encoder.prevClk && encoder.currClk == 1) {
        encoder.moved = true;
    }
    encoder.prevClk = encoder.currClk;
}

IRAM_ATTR void encoderPress() {
    if ((currMs - encoder.btn.prevMs) >= encoder.btn.intervalMs) {
        encoder.pressed = true;
    }
    encoder.btn.prevMs = currMs;
}

/*** initialization ***/

void initSerial() {
    Serial.begin(9600);
    Serial.println();

    for (uint8_t t = 3; t > 0; t--){
        Serial.printf("WAIT %d...\n", t);
        Serial.flush();
        delay(500);
    }
    Serial.println("\n* * * START * * *");
}

void initDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDR)) {
        errorHalt("SSD1306 allocation failed.");
    }
    delay(250);
    resetScreen();
}

void initWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_WIFI_SSID, _WIFI_PASS);

    Serial.printf("Connecting to WiFi [%s]", _WIFI_SSID);
    display.printf("Connecting to WiFi\n  %s ...", _WIFI_SSID);
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
        errorHalt("Error occurred while mounting LittleFS.");
    }
    delay(50);
}

void initConfig() {
    // TODO: read from file system
}

void initEncoder() {
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    attachInterrupt(ENCODER_CLK, encoderMove, CHANGE);

    pinMode(ENCODER_DT, INPUT_PULLUP);
    attachInterrupt(ENCODER_DT, encoderMove, CHANGE);

    pinMode(ENCODER_SW, INPUT);
    attachInterrupt(ENCODER_SW, encoderPress, FALLING);

    encoder.btn = {0, DEBOUNCE_MS};
    encoder.prevClk = digitalRead(ENCODER_CLK);
    encoder.moved = false;
    encoder.pressed = false;
}

/*** main ***/

void setup() {
    initSerial();
    initDisplay();
    initWifi();
    initFs();
    initConfig();
    initEncoder();

    ntpTimer = {0, NTP_INTERVAL_MS};
    updateDisplayTimer = {0, DISPLAY_INTERVAL_MS};

    pageRange = {STATE_IDLE_YEAR, STATE_SHOW_NTP};

    timeClient.begin();
    timeClient.setTimeOffset(-5 * 3600); 
    // TODO: EST for now (UTC-05:00), need to load from config.json

    resetScreen();
    display.display();
    resyncNTP();
    drawTime();

    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    currMs = millis();

    // resync NTP to keep local time accurate
    if ((currMs - ntpTimer.prevMs) >= ntpTimer.intervalMs) {
        resyncNTP();
        ntpTimer.prevMs = currMs;
    }

    // keep local time updated
    if ((currState == STATE_IDLE_YEAR || currState == STATE_IDLE_LIFE) && (currMs - updateDisplayTimer.prevMs) >= updateDisplayTimer.intervalMs) {
        epochTime += 1; // advance one second for local time
        drawPage();
        updateDisplayTimer.prevMs = currMs;
    }

    if (encoder.moved) {
        handleEncoderMove();
        encoder.moved = false;
    }

    if (encoder.pressed) {
        handleEncoderPress();
        encoder.pressed = false;
    }
}
