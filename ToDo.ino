#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Arduino.h>
#include "epd_driver.h"
#include "firasans.h"
#include "esp_adc_cal.h"
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TouchDrvGT911.hpp>
#include <SensorPCF8563.hpp>
#include <esp_sntp.h>
#include <ArduinoJson.h>
#include "utilities.h"
#include "displayPages.h"
#include "mainPage.h"
#include "todoPage.h"
#include "calendarPage.h"

const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;
const char *time_zone = "CEST-2";

SensorPCF8563 rtc;
TouchDrvGT911 touch;

#define SD_MISO 16
#define SD_MOSI 15
#define SD_SCLK 11
#define SD_CS 42

#define WIFI_SSID "UPC247757721"
#define WIFI_PASSWORD "ECY7365R"

uint8_t *framebuffer = NULL;
int currentPage = 0;
int currentTopic = -1;
std::vector<Topic> topics;

bool touchOnline = false;
uint32_t interval = 0;
int vref = 1100;
char buf[128];

int16_t x, y;
bool touchReleased = true;
time_t lastTimeUpdate = 0;

WebServer server(80);

void handleGetTodos() {
    File file = SD.open("/todo.json");
    if (!file) {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to open file\"}");
        return;
    }

    String jsonResponse = file.readString();
    server.send(200, "application/json", jsonResponse);
    file.close();
}

void handlePostTodos() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided\"}");
        return;
    }

    String body = server.arg("plain");
    File file = SD.open("/todo.json", FILE_WRITE);
    if (!file) {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to open file for writing\"}");
        return;
    }

    file.print(body);
    file.close();
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"File saved\"}");

    loadTodosFromSD();
    displayMainPage();
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting setup...");

    // Initialize WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    configTzTime(time_zone, ntpServer1, ntpServer2);

    if (MDNS.begin("showorld")) {
        Serial.println("MDNS responder started");
    }

    // Initialize SD card
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("SD init failed");
        return;
    }
    Serial.println("SD card initialized");

    // Initialize framebuffer
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("Framebuffer allocation failed");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    epd_init();

    Wire.begin(BOARD_SDA, BOARD_SCL);
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, HIGH);

    uint8_t touchAddress = 0;
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) {
        touchAddress = 0x14;
    }
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) {
        touchAddress = 0x5D;
    }
    if (touchAddress == 0) {
        while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
            delay(1000);
        }
    }

    touch.setPins(-1, TOUCH_INT);
    if (!touch.begin(Wire, touchAddress, BOARD_SDA, BOARD_SCL)) {
        while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
            delay(1000);
        }
    }

    touch.setMaxCoordinates(EPD_WIDTH, EPD_HEIGHT);
    touch.setSwapXY(true);
    touch.setMirrorXY(false, true);

    loadTodosFromSD();
    displayMainPage();

    server.on("/todo", HTTP_GET, handleGetTodos);
    server.on("/todo", HTTP_POST, handlePostTodos);
    server.begin();
}

void loop() {
    server.handleClient();

    uint8_t touched = touch.getPoint(&x, &y);
    if (touched) {
        if (touchReleased) {
            touchReleased = false;
            Serial.printf("Touch detected at: X:%d Y:%d\n", x, y);

            if (currentPage == 0 && x >= 15 && x <= 945 && y >= 20 && y <= 100) {
                displayCalendar();
                return;
            }

            if (currentPage == 0) {
                int yPos = 100;
                const int rectHeight = 120;
                const int gap = 20;
                for (int i = 0; i < 6; i++) {
                    int xMin = (i % 2 == 0) ? 50 : 550;
                    int xMax = xMin + 450;
                    int rectYMin = yPos;
                    int rectYMax = rectYMin + rectHeight;
                    if (x >= xMin && x <= xMax && y >= rectYMin && y <= rectYMax) {
                        displayTodoListPage(i);
                        return;
                    }
                    if (i % 2 == 1) {
                        yPos += rectHeight + gap;
                    }
                }
            } else if (currentPage == 1) {
                if (x >= 860 && x <= 960 && y >= 0 && y <= EPD_HEIGHT) {
                    displayMainPage();
                    return;
                }
                int yPos = (EPD_HEIGHT - (6 * 60 + 5 * 20)) / 2;
                const int rectHeight = 60;
                const int gap = 20;
                for (int i = 0; i < 6; i++) {
                    int rectYMin = yPos - 10;
                    int rectYMax = rectYMin + rectHeight;
                    if (x >= 40 && x <= 40 + 800 && y >= rectYMin && y <= rectYMax) {
                        toggleTodoItem(currentTopic, i);
                        return;
                    }
                    yPos += rectHeight + gap;
                }
            } else if (currentPage == 2) {
                if (x >= 860 && x <= 960 && y >= 0 && y <= EPD_HEIGHT) {
                    displayMainPage();
                    return;
                }
            }
        }
    } else {
        touchReleased = true;
    }

    if (millis() - lastTimeUpdate >= 3600000) {
        displayMainPage();
        lastTimeUpdate = millis();
    }

    delay(10);
}
