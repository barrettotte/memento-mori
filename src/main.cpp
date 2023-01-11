#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <limits.h>
#include <memory.h>
#include <SPI.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "config.h"
#include "hourglass.h"

#define CLOCK_BUFFER_SIZE 32
#define NTP_PACKET_SIZE 48
#define NTP_PORT 123

#define errorHalt(s) Serial.println(s); while(1) {}

enum state {
    STATE_IDLE_TIME,  // show current date/time
    STATE_IDLE_YEAR,  // show year percentage
    STATE_IDLE_LIFE,  // show life percentage
    STATE_SHOW_UTC,   // display UTC offset setting
    STATE_SHOW_BIRTH, // display current birth setting
    STATE_SHOW_DEATH, // display current death setting
    STATE_SHOW_NTP,   // display NTP refresh screen
    STATE_SET_UTC,    // set UTC offset
    STATE_SET_BIRTH,  // set birth date
    STATE_SET_DEATH,  // set estimated death date
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
    int utcOffset;
    time_t birth;
    time_t death;
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

WiFiUDP udp;
byte packetBuffer[NTP_PACKET_SIZE];
char clockBuffer[CLOCK_BUFFER_SIZE];
uint8_t utcOffset;

state prevState = STATE_IDLE_YEAR;
state currState = STATE_IDLE_TIME;
time_t prevTimeDisplayed = 0;
uint8_t hourglassIdx = 0;

range pageRange;

unsigned long currMs = 0;

/*** utilities ***/

void printTime() {
    Serial.printf(clockFormat, year(), month(), day(), hour(), minute(), second());
    Serial.println();
}

uint8_t loadConfig() {
    // TODO:
    return 0;
}

void saveConfig() {
    // TODO:
}

/*** NTP ***/

void sendNtpPacket(IPAddress &ip) {
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // https://www.meinbergglobal.com/english/info/ntp-packet.htm
    packetBuffer[0] = 0b11100011; // Leap Indicator (LI), version, mode
    packetBuffer[1] = 0;          // Stratum (type of clock) - 0=unspecified
    packetBuffer[2] = 6;          // Poll interval
    packetBuffer[3] = 0xEC;       // Precision
                                  // 8 zero bytes -> Root Delay and Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    udp.beginPacket(ip, NTP_PORT);
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}

time_t getNtpTime() {
    IPAddress ntpServerIp;
    Serial.println("Fetching time from NTP server.");

    while (udp.parsePacket() > 0) {
        // discard previously received packets
    }

    if (!WiFi.hostByName(ntpServer, ntpServerIp)) {
        Serial.printf("Error: DNS lookup failed for NTP server %s\n", ntpServer);
        return 0;
    }
    Serial.printf("%s:%s\n", ntpServer, ntpServerIp.toString().c_str());
    sendNtpPacket(ntpServerIp);

    uint32_t wait = millis();
    while (millis() - wait < NTP_WAIT_MS) {
        if (udp.parsePacket() >= NTP_PACKET_SIZE) {
            udp.read(packetBuffer, NTP_PACKET_SIZE);

            uint32_t t = (packetBuffer[40] << 24) | (packetBuffer[41] << 16)
                       | (packetBuffer[42] << 8)  | (packetBuffer[43]);
            return t - 2208988800UL + (config.utcOffset * SECS_PER_HOUR);
        }
    }
    Serial.println("Error: Failed to get time from NTP server.");
    return 0;
}

void resyncNtp() {
    Serial.println("Resync NTP.");
    setTime(getNtpTime());
    printTime();
}

/*** display ***/

void resetDisplay() {
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
    memset(clockBuffer, 0, CLOCK_BUFFER_SIZE);
    sprintf(clockBuffer, clockFormat, year(), month(), day(), hour(), minute(), second());
    drawCenteredText(clockBuffer, true, true);
}

void drawHourglassAnimation() {
    display.drawBitmap(
        (DISPLAY_WIDTH / 2) - (HOURGLASS_WIDTH / 2),
        (DISPLAY_HEIGHT / 2) - (HOURGLASS_HEIGHT / 2),
        hourglassFrames[hourglassIdx++], HOURGLASS_WIDTH, HOURGLASS_HEIGHT, 1);

    if (hourglassIdx >= HOURGLASS_FRAMES) {
        hourglassIdx = 0;
    }
}

void drawYearProgressPage() {
    drawCenteredText("Year Progress", true, false);
    drawHourglassAnimation();
}

void drawLifeProgressPage() {
    drawCenteredText("Life Progress", true, false);
    drawHourglassAnimation();
}

void drawPage() {
    resetDisplay();

    switch (currState) {
        case STATE_IDLE_TIME:
            drawTime();
            break;
        case STATE_IDLE_YEAR:
            drawYearProgressPage();
            break;
        case STATE_IDLE_LIFE:
            drawLifeProgressPage();
            break;
        case STATE_SHOW_UTC:
            drawCenteredText("UTC Offset", true, false);
            break;
        case STATE_SHOW_BIRTH:
            drawCenteredText("Birth Date", true, false);
            break;
        case STATE_SHOW_DEATH:
            drawCenteredText("Estimated", true, false);
            display.setCursor(0, display.getCursorY() + 1);
            drawCenteredText("Death Date", true, false);
            break;
        case STATE_SHOW_NTP:
            drawCenteredText("NTP Resync", true, true);
            break;
        case STATE_SET_UTC:
            drawCenteredText("Set UTC Offset", true, false);
            break;
        case STATE_SET_BIRTH:
            drawCenteredText("Set Birth Date", true, false);
            break;
        case STATE_SET_DEATH:
            drawCenteredText("Set Estimated", true, false);
            display.setCursor(0, display.getCursorY() + 1);
            drawCenteredText("Death Date", true, false);
            break;
        default:
            Serial.printf("Warning: Unnecessary page draw for state %d\n", currState);
            break;
    }
    display.display();
}

/*** encoder ***/

void scrollPage() {
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

void handleEncoderMove() {
    if (currState > 6) {
        // change setting (UTC offset, birth, death)
    } else {
        scrollPage();
    }
}

void handleEncoderPress() {
    switch (currState) {
        case STATE_SHOW_UTC:
            Serial.println("Started editing UTC offset!");
            currState = STATE_SET_UTC;
            break;
        case STATE_SHOW_BIRTH:
            Serial.println("Started editing birth date!");
            currState = STATE_SET_BIRTH;
            break;
        case STATE_SHOW_DEATH:
            Serial.println("Started editing death date!");
            currState = STATE_SET_DEATH;
            break;
        case STATE_SHOW_NTP:
            resyncNtp();
            currState = STATE_IDLE_TIME;
            drawPage();
            break;
        case STATE_SET_UTC:
            Serial.println("Done editing UTC offset!");
            currState = STATE_SHOW_UTC;
            break;
        case STATE_SET_BIRTH:
            Serial.println("Done editing birth date!");
            currState = STATE_SHOW_BIRTH;
            break;
        case STATE_SET_DEATH:
            Serial.println("Done editing death date!");
            currState = STATE_SHOW_DEATH;
            break;
        default:
            Serial.printf("Encoder pressed -> state=%d\n", currState);
            break;
    }
    if (currState >= 3) {
        drawPage();
    }
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
    resetDisplay();
    display.display();
}

void initWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_WIFI_SSID, _WIFI_PASS);

    Serial.printf("Connecting to WiFi [%s]", _WIFI_SSID);
    display.setCursor(0, 3);
    display.printf("Connecting to WiFi\n\n%s\n\n...", _WIFI_SSID);
    display.display();

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.printf(".");
    }
    Serial.printf("IP => %s\n", WiFi.localIP().toString().c_str());
    
    udp.begin(UDP_PORT);
    Serial.printf("Local port: %d\n", udp.localPort());

    resetDisplay();
    display.display();
}

void initFs() {
    if (!LittleFS.begin()) {
        errorHalt("Error occurred while mounting LittleFS.");
    }
    delay(50);
}

void initConfig() {
    // TODO: read from file system
    config.utcOffset = UTC_OFFSET_DEFAULT;
}

void initEncoder() {
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT);

    attachInterrupt(ENCODER_CLK, encoderMove, CHANGE);
    attachInterrupt(ENCODER_DT, encoderMove, CHANGE);
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

    // NTP sync
    setSyncProvider(getNtpTime);
    setSyncInterval(NTP_SYNC_SECS);

    // init globals
    pageRange = {STATE_IDLE_TIME, STATE_SHOW_NTP};

    drawTime();
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    currMs = millis();

    if (timeStatus() != timeNotSet) {
        if ((currState < 3) && now() != prevTimeDisplayed) {
            prevTimeDisplayed = now();
            drawPage();
        }
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
