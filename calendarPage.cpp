#include "calendarPage.h"
#include "displayPages.h"
#include "epd_driver.h"
#include "firasans.h"

void displayCalendar() {
    currentPage = 2;
    clearDisplay();

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int today = timeinfo.tm_mday;

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

    int xPos = 890;
    int yBackPos = 275;
    write_string((GFXfont *)&FiraSans, "✔️", &xPos, &yBackPos, framebuffer);
    epd_draw_rect(860, 0, 100, EPD_HEIGHT, 0x0000, framebuffer);

    struct tm firstDayOfMonth = {0};
    firstDayOfMonth.tm_year = year - 1900;
    firstDayOfMonth.tm_mon = month - 1;
    firstDayOfMonth.tm_mday = 1;
    mktime(&firstDayOfMonth);
    int firstDayOfWeek = firstDayOfMonth.tm_wday;
    if (firstDayOfWeek == 0) {
        firstDayOfWeek = 7;
    }

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

    const char *daysOfWeek[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    int xDayPos = 15;
    int yDayPos = 120;
    for (int i = 0; i < 7; i++) {
        int xTextPos = xDayPos + i * 118 + 5;
        int yTextPos = yDayPos + 40;
        write_string((GFXfont *)&FiraSans, daysOfWeek[i], &xTextPos, &yTextPos, framebuffer);
    }

    int xDayRectPos = 15;
    int yDayRectPos = 155;
    const int dayRectWidth = 100;
    const int dayRectHeight = 60;
    const int dayGap = 17;

    int currentDay = 1;
    for (int i = 0; i < 6; i++) { 
        for (int j = 0; j < 7; j++) { 
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

            if (currentDay == today) {
                epd_draw_rect(xRectPos, yRectPos + 12, dayRectWidth - 25, dayRectHeight - 10, 0xF800, framebuffer);
            }

            currentDay++;
        }
    }

    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}
