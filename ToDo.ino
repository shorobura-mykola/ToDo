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
bool touchOnline = false;
uint32_t interval = 0;
int vref = 1100;
char buf[128];

int16_t x, y;
bool touchReleased = true;
int currentPage = 0;
int currentTopic = -1;
time_t lastTimeUpdate = 0;

WebServer server(80);

struct TodoItem {
    String task;
    bool done;
};

struct Topic {
    String name;
    std::vector<TodoItem> todoList;
};

std::vector<Topic> topics;

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

    // Reload the todos after saving
    loadTodosFromSD();
    displayMainPage();
}

void setup() {
    Serial.begin(115200);

    // Initialize WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    if (MDNS.begin("showorld")) {
        Serial.println("MDNS responder started");
    }

    // Initialize SD card
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("SD init failed");
        return;
    }

    // Initialize framebuffer
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("alloc memory failed !!!");
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

    Serial.println("Started Touchscreen poll...");

    loadTodosFromSD();
    displayMainPage();

    server.on("/todo", HTTP_GET, handleGetTodos);
    server.on("/todo", HTTP_POST, handlePostTodos);

    server.begin();
    Serial.println("HTTP server started");
}

void loadTodosFromSD() {
    topics.clear(); // Clear existing topics

    File file = SD.open("/todo.json");
    if (!file) {
        Serial.println("Failed to open file");
        return;
    }

    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        Serial.print(F("Failed to read file, using default configuration"));
        Serial.println(error.c_str());
        file.close();
        return;
    }

    JsonArray topicsArray = doc["topics"];
    for (size_t i = 0; i < topicsArray.size(); i++) {
        JsonObject topic = topicsArray[i];
        Topic newTopic;
        newTopic.name = topic["name"].as<String>();
        JsonArray todoList = topic["todoList"];
        for (size_t j = 0; j < todoList.size(); j++) {
            JsonObject todoItem = todoList[j];
            TodoItem newTodoItem;
            newTodoItem.task = todoItem["task"].as<String>();
            newTodoItem.done = todoItem["done"].as<bool>();
            newTopic.todoList.push_back(newTodoItem);
        }
        topics.push_back(newTopic);
    }

    file.close();
}

void saveTodosToSD() {
    File file = SD.open("/todo.json", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    StaticJsonDocument<4096> doc;
    JsonArray topicsArray = doc.createNestedArray("topics");
    for (const auto& topic : topics) {
        JsonObject topicObject = topicsArray.createNestedObject();
        topicObject["name"] = topic.name;
        JsonArray todoList = topicObject.createNestedArray("todoList");
        for (const auto& todoItem : topic.todoList) {
            JsonObject todoObject = todoList.createNestedObject();
            todoObject["task"] = todoItem.task;
            todoObject["done"] = todoItem.done;
        }
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println(F("Failed to write to file"));
    }

    file.close();
}

void clearDisplay()
{
    epd_poweron();
    epd_clear();
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

void displayMainPage()
{
    currentPage = 0;
    currentTopic = -1;
    clearDisplay();
    int yPos = 115; // Start closer to the top
    int xPosLeft = 15; // Start closer to the left
    int xPosRight = 491; // Adjusted for smaller gap
    const int rectWidth = 453; // Wider topic rectangle
    const int rectHeight = 120;
    const int gap = 20; // gap between topic rows
    const int checkboxSize = 40;

    int xTitlePos = 15;
    int yTitlePos = 20;
    int titleWidth = 930;
    int titleHeight = 80;
    epd_draw_rect(xTitlePos, yTitlePos, titleWidth, titleHeight, 0x0000, framebuffer);

    int xTitleTextPos = 340;
    int yTitleTextPos = 75;

    // Get the current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    // Format the time
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%a %b %d %Y", &timeinfo);

    write_string((GFXfont *)&FiraSans, String(timeStr).c_str(), &xTitleTextPos, &yTitleTextPos, framebuffer);

    for (size_t i = 0; i < topics.size(); i++) {
        int xPos = (i % 2 == 0) ? xPosLeft : xPosRight;
        epd_draw_rect(xPos, yPos, rectWidth, rectHeight, 0x0000, framebuffer); // Draw rectangle around each topic

        // Center topic name within the rectangle
        int textXPos = xPos + 10; // Start text within the rectangle
        int textYPos = yPos + 50; // Position above the checkboxes
        write_string((GFXfont *)&FiraSans, topics[i].name.c_str(), &textXPos, &textYPos, framebuffer);

        int checkboxXPos = xPos + 10; // Start checkboxes from the left side of the rectangle
        for (size_t j = 0; j < topics[i].todoList.size(); j++) {
            int checkboxYPos = yPos + 70; // Position checkboxes below the topic name
            int doneYPos = checkboxYPos + 35;
            if (topics[i].todoList[j].done) {
                write_string((GFXfont *)&FiraSans, "✔️", &checkboxXPos, &doneYPos, framebuffer); // Done mark
                checkboxXPos += 8; // Space between checkboxes
            } else {
                epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize, 0x0000, framebuffer); // Empty checkbox
                checkboxXPos += checkboxSize + 10; // Space between checkboxes
            }
        }

        if (i % 2 == 1) {
            yPos += rectHeight + gap;
        }
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

void displayTodoListPage(int topicIndex)
{
    currentPage = topicIndex + 1;
    currentTopic = topicIndex;
    clearDisplay();

    // Calculate the y position for centering the to-do items
    int totalItemsHeight = topics[topicIndex].todoList.size() * 60 + (topics[topicIndex].todoList.size() - 1) * 20; // Adjusted for dynamic size
    int yPos = (EPD_HEIGHT - totalItemsHeight) / 2;

    // Draw back button
    int xPos = 890;
    int yBackPos = 275;
    write_string((GFXfont *)&FiraSans, "✔️", &xPos, &yBackPos, framebuffer);
    epd_draw_rect(860, 0, 100, EPD_HEIGHT, 0x0000, framebuffer); // Full height back button

    const int rectWidth = 800;
    const int rectHeight = 60;
    const int checkboxSize = 45; // Make checkbox bigger
    const int gap = 20;
    for (size_t i = 0; i < topics[topicIndex].todoList.size(); i++) {
        // Draw checkbox inside the to-do item rectangle
        int checkboxXPos = 55;
        int checkboxYPos = yPos; // Adjust position to not overlap
        if (topics[topicIndex].todoList[i].done) {
            epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize - 5, 0x0000, framebuffer); // Outer checkbox
            int checkmarkXPos = checkboxXPos + 5;
            int checkmarkYPos = checkboxYPos + (checkboxSize - 5) / 2 + 15;
            write_string((GFXfont *)&FiraSans, "✔️", &checkmarkXPos, &checkmarkYPos, framebuffer); // Checkmark inside checkbox
        } else {
            epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize - 5, 0x0000, framebuffer); // Outer checkbox
        }
        // Draw to-do item text inside the to-do rectangle
        xPos = 120;
        int textYPos = yPos + (rectHeight / 2) + 5; // Center text vertically within the to-do rectangle
        write_string((GFXfont *)&FiraSans, topics[topicIndex].todoList[i].task.c_str(), &xPos, &textYPos, framebuffer);
        epd_draw_rect(40, yPos - 10, rectWidth, rectHeight, 0x0000, framebuffer); // Draw rectangle around each item
        yPos += rectHeight + gap;
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

void toggleTodoItem(int topicIndex, int itemIndex)
{
    topics[topicIndex].todoList[itemIndex].done = !topics[topicIndex].todoList[itemIndex].done;
    saveTodosToSD();
    displayTodoListPage(topicIndex);
}

void loop()
{
    server.handleClient();
    uint8_t touched = touch.getPoint(&x, &y);
    if (touched) {
        if (touchReleased) {
            touchReleased = false;
            Serial.printf("Touch detected at: X:%d Y:%d\n", x, y);

            if (currentPage == 0) {
                // Main page
                int yPos = 100;
                const int rectHeight = 120;
                const int gap = 20;
                for (size_t i = 0; i < topics.size(); i++) {
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
            } else {
                // To-do list page
                if (x >= 860 && x <= 960 && y >= 0 && y <= EPD_HEIGHT) {
                    displayMainPage();
                    return;
                }
                // Check if any to-do item rectangle was pressed
                int yPos = (EPD_HEIGHT - (topics[currentTopic].todoList.size() * 60 + (topics[currentTopic].todoList.size() - 1) * 20)) / 2;
                const int rectHeight = 60;
                const int gap = 20;
                for (size_t i = 0; i < topics[currentTopic].todoList.size(); i++) {
                    int rectYMin = yPos - 10;
                    int rectYMax = rectYMin + rectHeight;
                    if (x >= 40 && x <= 40 + 800 && y >= rectYMin && y <= rectYMax) {
                        toggleTodoItem(currentTopic, i);
                        return;
                    }
                    yPos += rectHeight + gap;
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
