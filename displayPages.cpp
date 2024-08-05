#include "displayPages.h"
#include "epd_driver.h"
#include <ArduinoJson.h>
#include "todoPage.h"
#include <FS.h>
#include <SD.h>

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

void toggleTodoItem(int topicIndex, int itemIndex) {
    topics[topicIndex].todoList[itemIndex].done = !topics[topicIndex].todoList[itemIndex].done;

    // Sort the todoList to move completed tasks to the end
    std::sort(topics[topicIndex].todoList.begin(), topics[topicIndex].todoList.end(), [](const TodoItem &a, const TodoItem &b) {
        return !a.done && b.done;
    });

    saveTodosToSD();

    // Assuming there's a function to display the todo list page
    displayTodoListPage(topicIndex);
}
