#include "mainPage.h"
#include "displayPages.h"
#include "epd_driver.h"
#include "firasans.h"

void displayMainPage() 
{
    currentPage = 0;
    currentTopic = -1;
    clearDisplay();
    int yPos = 115; 
    int xPosLeft = 15; 
    int xPosRight = 491; 
    const int rectWidth = 453; 
    const int rectHeight = 120;
    const int gap = 20; 
    const int checkboxSize = 40;

    int xTitlePos = 15;
    int yTitlePos = 20;
    int titleWidth = 930;
    int titleHeight = 80;
    epd_draw_rect(xTitlePos, yTitlePos, titleWidth, titleHeight, 0x0000, framebuffer);

    int xTitleTextPos = 25;
    int yTitleTextPos = 75;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%VW                     %a %b %d %Y", &timeinfo);

    write_string((GFXfont *)&FiraSans, String(timeStr).c_str(), &xTitleTextPos, &yTitleTextPos, framebuffer);

    for (size_t i = 0; i < topics.size(); i++) {
        int xPos = (i % 2 == 0) ? xPosLeft : xPosRight;
        epd_draw_rect(xPos, yPos, rectWidth, rectHeight, 0x0000, framebuffer); 

        int textXPos = xPos + 10; 
        int textYPos = yPos + 50; 
        write_string((GFXfont *)&FiraSans, topics[i].name.c_str(), &textXPos, &textYPos, framebuffer);

        int checkboxXPos = xPos + 10; 
        for (size_t j = 0; j < topics[i].todoList.size(); j++) {
            int checkboxYPos = yPos + 70; 
            int doneYPos = checkboxYPos + 35;
            if (topics[i].todoList[j].done) {
                write_string((GFXfont *)&FiraSans, "✔️", &checkboxXPos, &doneYPos, framebuffer); 
                checkboxXPos += 8; 
            } else {
                epd_draw_rect(checkboxXPos, checkboxYPos, checkboxSize, checkboxSize, 0x0000, framebuffer); 
                checkboxXPos += checkboxSize + 10; 
            }
        }

        if (i % 2 == 1) {
            yPos += rectHeight + gap;
        }
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}
