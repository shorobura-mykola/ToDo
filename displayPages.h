#ifndef DISPLAYPAGES_H
#define DISPLAYPAGES_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

struct TodoItem {
    String task;
    bool done;
};

struct Topic {
    String name;
    std::vector<TodoItem> todoList;
};

extern uint8_t *framebuffer;
extern int currentPage;
extern int currentTopic;
extern std::vector<Topic> topics;

void loadTodosFromSD();
void saveTodosToSD();
void clearDisplay();
void toggleTodoItem(int topicIndex, int itemIndex);

#endif // DISPLAYPAGES_H
