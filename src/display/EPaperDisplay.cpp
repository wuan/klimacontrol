#include "display/EPaperDisplay.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <SPI.h>
#include <cstdio>
#include <esp_task_wdt.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "Log.h"
#include "display/DisplayPins.h"

static const char *TAG = "display";

namespace Display {

    namespace {

        // Paged rendering: the page buffer is (WIDTH / 8) * page_height bytes,
        // i.e. (200 / 8) * 25 = 625 B. It is a member array of this file-scope
        // object, so it lives in BSS — internal SRAM, allocated whether or not
        // the display is enabled.
        //
        // A full-screen buffer (page_height = HEIGHT) would be 5000 B. That is
        // the option that breaks something: measured steady-state internal free
        // is ~24 KB, and 5000 B of it would drop free below OTAUpdater's
        // MIN_FREE_INTERNAL gate of 20480 B, silently making firmware updates
        // impossible. 8 pages of text costs a few hundred microseconds.
        GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT / 8> display(
            GxEPD2_154_D67(DisplayPins::CS, DisplayPins::DC, DisplayPins::RST, DisplayPins::BUSY));

        // Layout (rotation 0; GFX transforms these for other rotations).
        //
        // The partial-refresh window spans the values AND the footer. The
        // footer's clock is not a wall clock — it is the timestamp of the
        // reading above it, so it has to be repainted whenever that reading is.
        // Leaving it outside the window (values only) froze it between full
        // refreshes, which on a stable sensor could be indefinitely.
        constexpr int16_t REFRESH_WINDOW_Y = 30;
        constexpr int16_t REFRESH_WINDOW_H = 160; // y 30..189, includes the footer
        constexpr int16_t PANEL_W = GxEPD2_154_D67::WIDTH;
        constexpr int16_t PANEL_H = GxEPD2_154_D67::HEIGHT;
        constexpr int16_t TEMP_BASELINE_Y = 85;
        constexpr int16_t HUMIDITY_BASELINE_Y = 128;
        constexpr int16_t FOOTER_RULE_Y = 160;
        constexpr int16_t FOOTER_TEXT_Y = 178;
        constexpr int16_t FOOTER_MARGIN_X = 6;

        // Geometry of the degree mark. The Adafruit GFX free fonts only carry
        // glyphs 0x20-0x7E, so U+00B0 (and the Latin-1 0xB0 byte) renders as
        // nothing — the ring has to be drawn, not printed.
        constexpr int16_t DEGREE_RADIUS = 6;
        constexpr int16_t DEGREE_GAP = 5;      // space between the digits and the ring
        constexpr int16_t DEGREE_TOP_INSET = 0; // below the cap height of the big font
        constexpr int16_t DEGREE_ADVANCE = DEGREE_GAP + 2 * DEGREE_RADIUS;

        // Draw `text` horizontally centred on the panel with its baseline at
        // `baselineY`, using whatever font is currently selected.
        void drawCentered(const char *text, int16_t baselineY) {
            int16_t x1 = 0;
            int16_t y1 = 0;
            uint16_t w = 0;
            uint16_t h = 0;
            display.getTextBounds(text, 0, baselineY, &x1, &y1, &w, &h);
            const int16_t x = static_cast<int16_t>((PANEL_W - static_cast<int16_t>(w)) / 2 - x1);
            display.setCursor(x, baselineY);
            display.print(text);
        }

        // Draw control state symbol at given position
        void drawControlSymbol(int16_t x, int16_t y, Display::ControlState state) {
            switch (state) {
                case Display::ControlState::INACTIVE:
                    display.drawFastHLine(x - 5, y, 10, GxEPD_BLACK);
                    break;
                case Display::ControlState::ACTIVE_OFF:
                    display.drawCircle(x, y, 6, GxEPD_BLACK);
                    break;
                case Display::ControlState::ACTIVE_ON:
                    display.fillCircle(x, y, 6, GxEPD_BLACK);
                    break;
            }
        }

        // Draw the temperature digits followed by a drawn degree ring, with the
        // digits-plus-ring group centred as a whole.
        //
        // NOTE on getTextBounds(): x1/y1 are ABSOLUTE coordinates of the
        // bounding box's top-left corner for a string drawn with the cursor at
        // the (x, y) passed in — they are not offsets from the baseline. Adding
        // baselineY to the returned y1 double-counts it and pushes the ring
        // roughly a full text height too low (past the humidity line, and
        // outside the partial-refresh window so it only shows up on a full
        // refresh and then sticks).
        void drawTemperatureWithDegree(const char *text, int16_t baselineY) {
            int16_t x1 = 0;
            int16_t y1 = 0;
            uint16_t w = 0;
            uint16_t h = 0;
            display.getTextBounds(text, 0, baselineY, &x1, &y1, &w, &h);

            const int16_t groupW = static_cast<int16_t>(w) + DEGREE_ADVANCE;
            // The -x1 here cancels the +x1 below, so the X axis is unaffected
            // by the absolute-vs-relative distinction.
            const int16_t x = static_cast<int16_t>((PANEL_W - groupW) / 2 - x1);
            display.setCursor(x, baselineY);
            display.print(text);

            const int16_t ringCx = static_cast<int16_t>(x + x1 + static_cast<int16_t>(w) +
                                                        DEGREE_GAP + DEGREE_RADIUS);
            // y1 is already the absolute top of the digits.
            const int16_t ringCy = static_cast<int16_t>(y1 + DEGREE_TOP_INSET + DEGREE_RADIUS);
            display.drawCircle(ringCx, ringCy, DEGREE_RADIUS + 1, GxEPD_BLACK);
            display.drawCircle(ringCx, ringCy, DEGREE_RADIUS, GxEPD_BLACK);
            display.drawCircle(ringCx, ringCy, DEGREE_RADIUS - 1, GxEPD_BLACK);
        }

        void drawRightAligned(const char *text, int16_t rightX, int16_t baselineY) {
            int16_t x1 = 0;
            int16_t y1 = 0;
            uint16_t w = 0;
            uint16_t h = 0;
            display.getTextBounds(text, 0, baselineY, &x1, &y1, &w, &h);
            display.setCursor(static_cast<int16_t>(rightX - static_cast<int16_t>(w) - x1), baselineY);
            display.print(text);
        }

        // Feeding the task watchdog is a no-op error when the calling task is
        // not subscribed (e.g. the setup task painting the splash), which is
        // fine — the return value is deliberately ignored.
        void feedWatchdog() {
            esp_task_wdt_reset();
        }

    } // namespace

    void EPaperDisplay::noteDuration(uint32_t elapsedMs, const char *what) {
        if (elapsedMs > REFRESH_TIMEOUT_MS) {
            consecutiveTimeouts++;
            ESP_LOGW(TAG, "%s took %u ms (> %u ms timeout), %u/%u consecutive",
                     what, elapsedMs, REFRESH_TIMEOUT_MS,
                     consecutiveTimeouts, MAX_CONSECUTIVE_TIMEOUTS);
            if (consecutiveTimeouts >= MAX_CONSECUTIVE_TIMEOUTS && !faulted) {
                faulted = true;
                // Logged once. The persisted DisplayConfig is deliberately NOT
                // touched: a loose connector must not silently rewrite the
                // user's settings, and a reboot re-tests the hardware.
                ESP_LOGE(TAG, "Display faulted after %u consecutive timeouts - "
                              "check the BUSY line and the panel connection. "
                              "No further refreshes until restart.",
                         consecutiveTimeouts);
            }
        } else {
            consecutiveTimeouts = 0;
        }
    }

    bool EPaperDisplay::begin(uint8_t rotation) {
        // Bring the bus up explicitly so the pin assignment is owned here
        // rather than by GxEPD2's internals. GxEPD2_EPD::init() calls
        // _pSPIx->begin() itself (GxEPD2_EPD.cpp:66), but SPIClass::begin()
        // early-returns when the bus is already started, so this call wins.
        //
        // MISO is passed as the real pin rather than -1 because the core
        // substitutes GPIO37 either way (esp32-hal-spi.c:203) — being explicit
        // documents that the pin is claimed and unread. This is also why CS
        // must not live on GPIO37; see the comment in DisplayPins.h.
        SPI.begin(DisplayPins::SCK, MISO, DisplayPins::MOSI, -1);

        const uint32_t start = millis();
        feedWatchdog();
        // serial_diag_bitrate = 0 so GxEPD2 does not call Serial.begin() and
        // disturb the USB CDC console this board logs to.
        display.init(0, true, 2, false);
        feedWatchdog();
        noteDuration(millis() - start, "Display init");

        display.setRotation(rotation);
        display.setTextColor(GxEPD_BLACK);
        display.setTextWrap(false);
        initialised = true;

        ESP_LOGI(TAG, "E-paper display initialised (rotation=%u, page buffer=%u B)",
                 rotation, static_cast<unsigned>((GxEPD2_154_D67::WIDTH / 8) * (GxEPD2_154_D67::HEIGHT / 8)));

        hibernate();
        return !faulted;
    }

    void EPaperDisplay::runPagedDraw(const char *tempStr, const char *humStr,
                                     const char *footerLeft, const char *footerRight,
                                     Display::ControlState controlState, const char *setpointStr) {
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);

            display.setFont(&FreeSansBold24pt7b);
            drawTemperatureWithDegree(tempStr, TEMP_BASELINE_Y);

            display.setFont(&FreeSans12pt7b);
            char humLine[24];
            snprintf(humLine, sizeof(humLine), "%s %%rH", humStr);
            drawCentered(humLine, HUMIDITY_BASELINE_Y);

            // Drawn on every refresh, partial included: the footer's right-hand
            // field timestamps the reading above it, so it must never be older
            // than that reading.
            display.drawFastHLine(FOOTER_MARGIN_X, FOOTER_RULE_Y,
                                  PANEL_W - 2 * FOOTER_MARGIN_X, GxEPD_BLACK);
            
            // Draw setpoint, degree symbol, and control symbol in footer center
            // Layout: [22.0][°][symbol] centered at x=100
            // Symbol to the right avoids confusion with negative numbers
            display.setFont(&FreeSans9pt7b);
            int16_t x1 = 0, y1 = 0;
            uint16_t w = 0, h = 0;
            display.getTextBounds(setpointStr, 0, FOOTER_TEXT_Y, &x1, &y1, &w, &h);
            
            // Measure total width: setpoint + degree circle + symbol
            // Symbol width: 12px for circle (diameter), 10px for line
            int16_t symbolWidth = 12; // All symbols fit within 12px
            int16_t degreeWidth = 4;  // Degree circle diameter
            int16_t spacing = 4;      // Space between elements
            int16_t totalWidth = w + degreeWidth + symbolWidth + 2 * spacing;
            
            // Center the group at x=100
            int16_t groupStartX = 100 - totalWidth / 2;
            
            // Draw setpoint text
            display.setCursor(groupStartX, FOOTER_TEXT_Y);
            display.print(setpointStr);
            
            // Draw degree circle to the right of setpoint (higher position for better alignment)
            int16_t degreeX = groupStartX + w - x1 + spacing;
            display.drawCircle(degreeX, 173, 2, GxEPD_BLACK); // y=173 is higher than 175
            
            // Draw control symbol to the right of degree circle
            int16_t symbolX = degreeX + degreeWidth + spacing;
            drawControlSymbol(symbolX, 175, controlState);
            
            // Draw device name and clock with same 9pt font
            if (footerLeft != nullptr && footerLeft[0] != '\0') {
                display.setCursor(FOOTER_MARGIN_X, FOOTER_TEXT_Y);
                display.print(footerLeft);
            }
            if (footerRight != nullptr && footerRight[0] != '\0') {
                drawRightAligned(footerRight, PANEL_W - FOOTER_MARGIN_X, FOOTER_TEXT_Y);
            }
        } while (display.nextPage());
    }

    void EPaperDisplay::render(const char *tempStr, const char *humStr,
                               const char *footerLeft, const char *footerRight,
                               Display::ControlState controlState, const char *setpointStr,
                               RefreshKind kind) {
        if (!initialised || faulted || kind == RefreshKind::None) {
            return;
        }

        const bool full = (kind == RefreshKind::Full);
        if (full) {
            display.setFullWindow();
        } else {
            // Everything that changes between refreshes — the values and the
            // footer timestamp — lives inside this window. Only the top margin
            // is excluded, which is blank.
            display.setPartialWindow(0, REFRESH_WINDOW_Y, PANEL_W, REFRESH_WINDOW_H);
        }

        // The paged loop blocks on the panel's BUSY line for ~0.5 s (partial)
        // to ~2.6 s (full). It runs on the Network task, so the watchdog is fed
        // immediately before and after, per the system-architecture
        // "blocking external call" requirement.
        const uint32_t start = millis();
        feedWatchdog();
        runPagedDraw(tempStr, humStr, footerLeft, footerRight, controlState, setpointStr);
        feedWatchdog();
        const uint32_t elapsed = millis() - start;

        noteDuration(elapsed, full ? "Full refresh" : "Partial refresh");
        ESP_LOGD(TAG, "%s refresh: '%s' / '%s' in %u ms",
                 full ? "Full" : "Partial", tempStr, humStr, elapsed);

        hibernate();
    }

    void EPaperDisplay::clear() {
        if (!initialised || faulted) {
            return;
        }

        const uint32_t start = millis();
        feedWatchdog();
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
        } while (display.nextPage());
        feedWatchdog();
        noteDuration(millis() - start, "Clear");

        ESP_LOGI(TAG, "Display cleared");
        hibernate();
    }

    void EPaperDisplay::showSplash(const char *deviceName) {
        if (!initialised || faulted) {
            return;
        }

        const uint32_t start = millis();
        feedWatchdog();
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);

            display.setFont(&FreeSans12pt7b);
            drawCentered("KlimaControl", 90);

            display.setFont(nullptr);
            if (deviceName != nullptr && deviceName[0] != '\0') {
                drawCentered(deviceName, 115);
            }
            drawCentered("starting...", 135);
        } while (display.nextPage());
        feedWatchdog();
        noteDuration(millis() - start, "Splash");

        ESP_LOGI(TAG, "Splash shown");
        hibernate();
    }

    void EPaperDisplay::hibernate() {
        if (!initialised) {
            return;
        }
        // Panel retains its image with no power draw.
        display.hibernate();
    }

} // namespace Display

#endif // ARDUINO
