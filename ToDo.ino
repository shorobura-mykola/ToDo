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
    currentPage = 1;
    currentTopic = topicIndex;
    clearDisplay();

    // Draw back button
    int xPos = 890;
    int yBackPos = 275;
    write_string((GFXfont *)&FiraSans, "✔️", &xPos, &yBackPos, framebuffer);
    epd_draw_rect(860, 0, 100, EPD_HEIGHT, 0x0000, framebuffer);

    const int rectWidth = 835;
    const int rectHeight = 60;
    const int checkboxSize = 40;
    const int gap = 15;
    int yPos = 20; // Start from the top
    for (size_t i = 0; i < topics[topicIndex].todoList.size(); i++) {
        // Draw checkbox inside the to-do item rectangle
        int checkboxXPos = 27;
        int checkboxYPos = yPos + 3; // Adjust position to not overlap
        if (topics[topicIndex].todoList[i].done) {
            epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize - 5, 0x0000, framebuffer); // Outer checkbox
            int checkmarkXPos = checkboxXPos + 2;
            int checkmarkYPos = checkboxYPos + (checkboxSize - 5) / 2 + 15;
            write_string((GFXfont *)&FiraSans, "✔️", &checkmarkXPos, &checkmarkYPos, framebuffer); // Checkmark inside checkbox
        } else {
            epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize - 5, 0x0000, framebuffer); // Outer checkbox
        }
        // Draw to-do item text inside the to-do rectangle
        xPos = 75;
        int textYPos = yPos + (rectHeight / 2) + 5; // Center text vertically within the to-do rectangle
        write_string((GFXfont *)&FiraSans, topics[topicIndex].todoList[i].task.c_str(), &xPos, &textYPos, framebuffer);
        epd_draw_rect(15, yPos - 10, rectWidth, rectHeight, 0x0000, framebuffer); // Draw rectangle around each item
        yPos += rectHeight + gap;
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

void displayCalendar() {
    currentPage = 2;
    clearDisplay();

    // Get the current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    // Get the current year, month, and day
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int today = timeinfo.tm_mday;

    // Calculate the number of days in the current month
    int daysInMonth = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        daysInMonth = 30;
    } else if (month == 2) {
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            daysInMonth = 29;
        } else {
            daysInMonth = 28;
        }
    }

    // Draw back button
    int xPos = 890;
    int yBackPos = 275;
    write_string((GFXfont *)&FiraSans, "✔️", &xPos, &yBackPos, framebuffer);
    epd_draw_rect(860, 0, 100, EPD_HEIGHT, 0x0000, framebuffer);

    // Calculate the day of the week for the first day of the month
    struct tm firstDayOfMonth = {0};
    firstDayOfMonth.tm_year = year - 1900;
    firstDayOfMonth.tm_mon = month - 1;
    firstDayOfMonth.tm_mday = 1;
    mktime(&firstDayOfMonth);
    int firstDayOfWeek = firstDayOfMonth.tm_wday;
    if (firstDayOfWeek == 0) {
        firstDayOfWeek = 7;
    }

    // Draw the calendar header
    int xHeaderPos = 15;
    int yHeaderPos = 20;
    int headerWidth = 835;
    int headerHeight = 80;
    epd_draw_rect(xHeaderPos, yHeaderPos, headerWidth, headerHeight, 0x0000, framebuffer);

    char headerStr[64];
    strftime(headerStr, sizeof(headerStr), "%B %Y", &timeinfo);
    int xHeaderTextPos = 350;
    int yHeaderTextPos = 70;
    write_string((GFXfont *)&FiraSans, String(headerStr).c_str(), &xHeaderTextPos, &yHeaderTextPos, framebuffer);

    // Draw the days of the week
    const char *daysOfWeek[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    int xDayPos = 15;
    int yDayPos = 120;
    for (int i = 0; i < 7; i++) {
        int xTextPos = xDayPos + i * 118 + 5;
        int yTextPos = yDayPos + 40;
        write_string((GFXfont *)&FiraSans, daysOfWeek[i], &xTextPos, &yTextPos, framebuffer);
    }

    // Draw the days of the month
    int xDayRectPos = 15;
    int yDayRectPos = 155;
    const int dayRectWidth = 100;
    const int dayRectHeight = 60;
    const int dayGap = 17;

    int currentDay = 1;
    for (int i = 0; i < 6; i++) { // 6 weeks in a month
        for (int j = 0; j < 7; j++) { // 7 days a week
            if (i == 0 && j < firstDayOfWeek - 1) {
                continue;
            }
            if (currentDay > daysInMonth) {
                break;
            }

            int xRectPos = xDayRectPos + j * (dayRectWidth + dayGap);
            int yRectPos = yDayRectPos + i * (dayRectHeight + dayGap);

            int xTextPos = xRectPos + 25;
            int yTextPos = yRectPos + 50;
            char dayStr[3];
            sprintf(dayStr, "%d", currentDay);
            write_string((GFXfont *)&FiraSans, dayStr, &xTextPos, &yTextPos, framebuffer);

            // Highlight today's date
            if (currentDay == today) {
                epd_draw_rect(xRectPos, yRectPos + 12, dayRectWidth - 25, dayRectHeight - 10, 0xF800, framebuffer); // Red border
            }

            currentDay++;
        }
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

void toggleTodoItem(int topicIndex, int itemIndex)
{
    topics[topicIndex].todoList[itemIndex].done = !topics[topicIndex].todoList[itemIndex].done;

    // Sort the todoList to move completed tasks to the end
    std::sort(topics[topicIndex].todoList.begin(), topics[topicIndex].todoList.end(), [](const TodoItem &a, const TodoItem &b) {
        return !a.done && b.done;
    });

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

            // Check if the title date rectangle is pressed
            if (currentPage == 0 && x >= 15 && x <= 945 && y >= 20 && y <= 100) {
                displayCalendar();
                return;
            }

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
            } else if (currentPage == 1) {
                // To-do list page
                if (x >= 860 && x <= 960 && y >= 0 && y <= EPD_HEIGHT) {
                    displayMainPage();
                    return;
                }
                // Check if any to-do item rectangle was pressed
                int yPos = 20; // Start from the top
                const int rectHeight = 60;
                const int gap = 15;
                for (size_t i = 0; i < topics[currentTopic].todoList.size(); i++) {
                    int rectYMin = yPos - 10;
                    int rectYMax = rectYMin + rectHeight;
                    if (x >= 40 && x <= 40 + 800 && y >= rectYMin && y <= rectYMax) {
                        toggleTodoItem(currentTopic, i);
                        return;
                    }
                    yPos += rectHeight + gap;
                }
            } else if (currentPage == 2) {
                // Calendar page
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
