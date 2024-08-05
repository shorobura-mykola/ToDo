#include "todoPage.h"
#include "displayPages.h"
#include "epd_driver.h"
#include "firasans.h"

void displayTodoListPage(int topicIndex)
{
    currentPage = 1;
    currentTopic = topicIndex;
    clearDisplay();

    int xPos = 890;
    int yBackPos = 275;
    write_string((GFXfont *)&FiraSans, "✔️", &xPos, &yBackPos, framebuffer);
    epd_draw_rect(860, 0, 100, EPD_HEIGHT, 0x0000, framebuffer);

    const int rectWidth = 835;
    const int rectHeight = 60;
    const int checkboxSize = 40;
    const int gap = 15;
    int yPos = 20; 
    for (size_t i = 0; i < topics[topicIndex].todoList.size(); i++) {
        int checkboxXPos = 27;
        int checkboxYPos = yPos + 3; 
        if (topics[topicIndex].todoList[i].done) {
            epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize - 5, 0x0000, framebuffer); 
            int checkmarkXPos = checkboxXPos + 2;
            int checkmarkYPos = checkboxYPos + (checkboxSize - 5) / 2 + 15;
            write_string((GFXfont *)&FiraSans, "✔️", &checkmarkXPos, &checkmarkYPos, framebuffer); 
        } else {
            epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize - 5, 0x0000, framebuffer); 
        }
        xPos = 75;
        int textYPos = yPos + (rectHeight / 2) + 5; 
        write_string((GFXfont *)&FiraSans, topics[topicIndex].todoList[i].task.c_str(), &xPos, &textYPos, framebuffer);
        epd_draw_rect(15, yPos - 10, rectWidth, rectHeight, 0x0000, framebuffer); 
        yPos += rectHeight + gap;
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}
