#include "display/EPaperDisplay.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>
#include <esp_task_wdt.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "Log.h"
#include "OTAConfig.h" // FIRMWARE_VERSION
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
        // The partial-refresh window spans the values AND the whole footer. The
        // footer's date/time is a live wall clock, so it has to be repainted
        // whenever the values are. Leaving any of it outside the window froze it
        // between full refreshes, which on a stable sensor could be indefinitely
        // — hence the window reaching to the bottom of the panel now that the
        // footer is two lines tall.
        constexpr int16_t PANEL_W = GxEPD2_154_D67::WIDTH;
        constexpr int16_t PANEL_H = GxEPD2_154_D67::HEIGHT;
        constexpr int16_t REFRESH_WINDOW_Y = 30;
        constexpr int16_t REFRESH_WINDOW_H = PANEL_H - REFRESH_WINDOW_Y; // y 30..199
        constexpr int16_t TEMP_BASELINE_Y = 85;
        constexpr int16_t HUMIDITY_BASELINE_Y = 128;

        // Header band, y 0..29: the strip above the partial-refresh window.
        //
        //   KlimaControl                    v0.1.1
        //
        //   ------ REFRESH_WINDOW_Y = 30 ------------
        //
        // This is the one region GxEPD2 never rewrites on a partial refresh
        // (drawPixel transposes by _pw_y and drops negative y, GxEPD2_BW.h:293),
        // so the band costs nothing on the common path — and may hold ONLY
        // content that cannot change while the firmware runs. FIRMWARE_VERSION
        // qualifies: it is a compile-time constant, and the first paint after
        // every boot is a Full refresh (RefreshPolicy.cpp), including the boot
        // that follows an OTA update. The device name does NOT qualify — the web
        // UI can change it without a reboot, which is why it stays in the footer.
        //
        // Both fields use the built-in 5x7 GFX font, which takes setCursor()'s
        // y as the glyph TOP rather than the baseline (the free fonts elsewhere
        // in this file take it as the baseline — the two are not
        // interchangeable). One font means one constant for the whole row.
        //
        // Ink: y 4..10, tucked against the top edge and well clear of the
        // window at y=30.
        constexpr const char *HEADER_TITLE = "KlimaControl";
        constexpr int16_t HEADER_TOP_Y = 4;
        constexpr int16_t HEADER_COLUMN_GAP = 6;

        // Boot splash: the device name over a status line, both centred. The
        // brand mark is not repeated here — the header band above already
        // carries it, so the splash reads as a state of the normal layout
        // rather than a separate screen. With the old centred title gone, the
        // remaining pair is centred in the space between the band and the
        // bottom margin instead of staying where the three-line block sat.
        constexpr int16_t SPLASH_NAME_Y = 104;   // 12pt baseline
        constexpr int16_t SPLASH_STATUS_Y = 126; // built-in font: glyph top

        // Two-line footer, two columns:
        //
        //   ---------------------------------------------  <- FOOTER_RULE_Y
        //   device-name                        22.0 (o)     <- FOOTER_LINE1_Y
        //   2026-09-01 12:34                        (*)     <- FOOTER_LINE2_Y
        //
        // Everything is either flush left at FOOTER_MARGIN_X or flush right at
        // FOOTER_RIGHT_X, so neither column drifts as its content changes width.
        // Baselines are 22 px apart, matching FreeSans9pt7b's yAdvance: ink runs
        // 12 px above the baseline and descenders 4 px below, so consecutive
        // lines clear each other by 6 px.
        constexpr int16_t FOOTER_RULE_Y = 152;
        constexpr int16_t FOOTER_LINE1_Y = 170; // baseline: name | setpoint
        constexpr int16_t FOOTER_LINE2_Y = 192; // baseline: date/time | symbol
        constexpr int16_t FOOTER_MARGIN_X = 6;
        constexpr int16_t FOOTER_RIGHT_X = PANEL_W - FOOTER_MARGIN_X;
        // Clear space kept between the left column's text and the right column,
        // i.e. the amount the left field is truncated to stay out of.
        constexpr int16_t FOOTER_COLUMN_GAP = 6;

        // Right column. The setpoint is clamped to 10..30 C, so it is never
        // negative and the symbol below it cannot read as a minus sign.
        constexpr int16_t SETPOINT_DEGREE_RADIUS = 2;
        constexpr int16_t SETPOINT_DEGREE_GAP = 4; // digits -> degree ring
        // The ring sits at the digits' cap height (their ink starts 12 px above
        // the baseline), like a real degree mark. Centring it on the digits
        // instead makes it read as a lowercase 'o'.
        constexpr int16_t SETPOINT_DEGREE_CY = FOOTER_LINE1_Y - 10;
        // Demand bar, footer line 2, immediately left of the control symbol.
        // Sized so the date/time beside it still fits: 5 segments of 6 px with
        // 2 px gaps is 38 px, leaving the left column ~126 px for a
        // "2026-09-02 14:07" that measures about 112 px at 9 pt.
        constexpr int16_t DEMAND_SEG_W = 6;
        constexpr int16_t DEMAND_SEG_H = 9;
        constexpr int16_t DEMAND_SEG_GAP = 2;
        constexpr int16_t DEMAND_BAR_GAP = 6; // bar -> control symbol
        constexpr int16_t DEMAND_BAR_W =
            Display::DEMAND_BUCKETS * DEMAND_SEG_W + (Display::DEMAND_BUCKETS - 1) * DEMAND_SEG_GAP;

        constexpr int16_t CONTROL_SYMBOL_RADIUS = 6;    // 12 px diameter circle
        constexpr int16_t CONTROL_SYMBOL_HALF_LINE = 5; // 10 px line for INACTIVE
        constexpr int16_t CONTROL_SYMBOL_CY = FOOTER_LINE2_Y - 5;

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
                    display.drawFastHLine(x - CONTROL_SYMBOL_HALF_LINE, y,
                                          2 * CONTROL_SYMBOL_HALF_LINE, GxEPD_BLACK);
                    break;
                case Display::ControlState::ACTIVE_OFF:
                    display.drawCircle(x, y, CONTROL_SYMBOL_RADIUS, GxEPD_BLACK);
                    break;
                case Display::ControlState::ACTIVE_ON:
                    display.fillCircle(x, y, CONTROL_SYMBOL_RADIUS, GxEPD_BLACK);
                    break;
                case Display::ControlState::UNCERTAIN:
                    // A ring with a dot: recognisably related to the other two
                    // without being mistakable for either. The GFX fonts carry
                    // no glyph worth using at this size, so it is drawn.
                    display.drawCircle(x, y, CONTROL_SYMBOL_RADIUS, GxEPD_BLACK);
                    display.fillCircle(x, y, 2, GxEPD_BLACK);
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

        // Advance width of `text` in the currently selected font.
        int16_t textWidth(const char *text) {
            int16_t x1 = 0;
            int16_t y1 = 0;
            uint16_t w = 0;
            uint16_t h = 0;
            display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
            return static_cast<int16_t>(w);
        }

        // Copy as much of `text` into `out` as fits within `maxWidth` pixels in
        // the current font, marking a cut with a trailing '.'.
        //
        // setTextWrap(false) means an over-long string is not clipped, it just
        // keeps drawing — a 31-character device_name in the 9pt font runs across
        // the centred setpoint group and paints over it. Truncating here is what
        // keeps the three footer fields from colliding. ('…' is not an option:
        // the GFX free fonts only carry glyphs 0x20-0x7E.)
        void fitToWidth(const char *text, int16_t maxWidth, char *out, size_t outSize) {
            if (out == nullptr || outSize == 0) {
                return;
            }
            out[0] = '\0';
            if (text == nullptr || maxWidth <= 0) {
                return;
            }
            strlcpy(out, text, outSize);
            if (textWidth(out) <= maxWidth) {
                return;
            }
            size_t len = strlen(out);
            while (len > 0) {
                len--;
                out[len] = '\0';
                if (len == 0) {
                    return; // not even one character plus the marker fits
                }
                const char cut = out[len - 1];
                out[len - 1] = '.';
                if (textWidth(out) <= maxWidth) {
                    return;
                }
                out[len - 1] = cut;
            }
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

    void EPaperDisplay::drawDemandBar(int16_t leftX, uint8_t filledSegments) {
        // Outlined segments for the empty part, solid for the filled part. An
        // outline rather than nothing so the scale is legible: five boxes make
        // it obvious that three filled means roughly 60 %, where three floating
        // blobs would not.
        const int16_t top = static_cast<int16_t>(FOOTER_LINE2_Y - DEMAND_SEG_H);
        for (uint8_t i = 0; i < Display::DEMAND_BUCKETS; ++i) {
            const int16_t x =
                static_cast<int16_t>(leftX + i * (DEMAND_SEG_W + DEMAND_SEG_GAP));
            if (i < filledSegments) {
                display.fillRect(x, top, DEMAND_SEG_W, DEMAND_SEG_H, GxEPD_BLACK);
            } else {
                display.drawRect(x, top, DEMAND_SEG_W, DEMAND_SEG_H, GxEPD_BLACK);
            }
        }
    }

    void EPaperDisplay::drawHeader() {
        // The version is the field that gives way, the inverse of the footer's
        // rule below, because here it is the right-hand field that varies:
        // "v0.1.1" tagged, "v0.0.0-dev" as the fallback, "v0.1.1-5-gc1c08f0"
        // from git describe. Tail truncation keeps the release prefix and drops
        // the build suffix, which is the right way round. The brand mark is a
        // fixed string and is never cut.
        //
        // At 6 px per character the 12-character title leaves 110 px, i.e. 18
        // characters, so every version form currently in use fits whole — the
        // truncation below is a guard against a future scheme, not the normal
        // path. It was the normal path while the title was set in 9 pt (103 px,
        // leaving only 13 characters), which is part of why it no longer is.
        display.setFont(nullptr);
        const int16_t titleW = textWidth(HEADER_TITLE);

        char version[24];
        const int16_t versionMaxW = static_cast<int16_t>(FOOTER_RIGHT_X - HEADER_COLUMN_GAP -
                                                         FOOTER_MARGIN_X - titleW);
        fitToWidth(FIRMWARE_VERSION, versionMaxW, version, sizeof(version));
        if (version[0] != '\0') {
            drawRightAligned(version, FOOTER_RIGHT_X, HEADER_TOP_Y);
        }

        display.setCursor(FOOTER_MARGIN_X, HEADER_TOP_Y);
        display.print(HEADER_TITLE);
    }

    void EPaperDisplay::runPagedDraw(const char *tempStr, const char *humStr,
                                     const char *footerName, const char *footerDateTime,
                                     Display::ControlState controlState, const char *setpointStr,
                                     uint8_t demandSegments) {
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);

            // Above the partial window, so this is a no-op on a partial
            // refresh and only the full refreshes repaint it.
            drawHeader();

            display.setFont(&FreeSansBold24pt7b);
            drawTemperatureWithDegree(tempStr, TEMP_BASELINE_Y);

            display.setFont(&FreeSans12pt7b);
            char humLine[24];
            snprintf(humLine, sizeof(humLine), "%s %%rH", humStr);
            drawCentered(humLine, HUMIDITY_BASELINE_Y);

            // Drawn on every refresh, partial included: the footer carries a
            // live clock, so it must never be older than the values above it.
            display.drawFastHLine(FOOTER_MARGIN_X, FOOTER_RULE_Y,
                                  PANEL_W - 2 * FOOTER_MARGIN_X, GxEPD_BLACK);

            display.setFont(&FreeSans9pt7b);

            // --- right column, drawn first: it fixes how much room the left
            // column has, and it is the field that must never be truncated ---

            // Line 1: setpoint, then the degree ring flush with the right margin.
            const int16_t ringCx = static_cast<int16_t>(FOOTER_RIGHT_X - SETPOINT_DEGREE_RADIUS);
            display.drawCircle(ringCx, SETPOINT_DEGREE_CY, SETPOINT_DEGREE_RADIUS, GxEPD_BLACK);

            const int16_t setpointRightX =
                static_cast<int16_t>(ringCx - SETPOINT_DEGREE_RADIUS - SETPOINT_DEGREE_GAP);
            drawRightAligned(setpointStr, setpointRightX, FOOTER_LINE1_Y);
            const int16_t setpointLeftX =
                static_cast<int16_t>(setpointRightX - textWidth(setpointStr));

            // Line 2: control symbol, also flush with the right margin.
            const int16_t symbolCx = static_cast<int16_t>(FOOTER_RIGHT_X - CONTROL_SYMBOL_RADIUS);
            drawControlSymbol(symbolCx, CONTROL_SYMBOL_CY, controlState);
            const int16_t symbolLeftX = static_cast<int16_t>(symbolCx - CONTROL_SYMBOL_RADIUS);

            // Demand bar, left of the symbol. Only drawn while control is
            // enabled: when it is off the minus symbol already says everything,
            // and an empty bar would just be clutter. When enabled the bar is
            // drawn even at zero demand, because "enabled but asking for
            // nothing" is worth distinguishing from "switched off".
            int16_t rightColumnLeftX = symbolLeftX;
            if (controlState != Display::ControlState::INACTIVE &&
                controlState != Display::ControlState::UNCERTAIN) {
                const int16_t barRightX = static_cast<int16_t>(symbolLeftX - DEMAND_BAR_GAP);
                const int16_t barLeftX = static_cast<int16_t>(barRightX - DEMAND_BAR_W);
                drawDemandBar(barLeftX, demandSegments);
                rightColumnLeftX = barLeftX;
            }

            // --- left column, each line truncated to what its own row leaves ---
            char footerField[40];
            if (footerName != nullptr && footerName[0] != '\0') {
                const int16_t maxWidth = static_cast<int16_t>(setpointLeftX - FOOTER_COLUMN_GAP -
                                                              FOOTER_MARGIN_X);
                fitToWidth(footerName, maxWidth, footerField, sizeof(footerField));
                if (footerField[0] != '\0') {
                    display.setCursor(FOOTER_MARGIN_X, FOOTER_LINE1_Y);
                    display.print(footerField);
                }
            }
            if (footerDateTime != nullptr && footerDateTime[0] != '\0') {
                const int16_t maxWidth = static_cast<int16_t>(rightColumnLeftX -
                                                              FOOTER_COLUMN_GAP - FOOTER_MARGIN_X);
                fitToWidth(footerDateTime, maxWidth, footerField, sizeof(footerField));
                if (footerField[0] != '\0') {
                    display.setCursor(FOOTER_MARGIN_X, FOOTER_LINE2_Y);
                    display.print(footerField);
                }
            }
        } while (display.nextPage());
    }

    void EPaperDisplay::render(const char *tempStr, const char *humStr,
                               const char *footerName, const char *footerDateTime,
                               Display::ControlState controlState, const char *setpointStr,
                               uint8_t demandSegments,
                               RefreshKind kind) {
        if (!initialised || faulted || kind == RefreshKind::None) {
            return;
        }

        const bool full = (kind == RefreshKind::Full);
        if (full) {
            display.setFullWindow();
        } else {
            // Everything that changes between refreshes — the values and both
            // footer lines — lives inside this window. Only the top margin is
            // excluded, which is blank.
            display.setPartialWindow(0, REFRESH_WINDOW_Y, PANEL_W, REFRESH_WINDOW_H);
        }

        // The paged loop blocks on the panel's BUSY line for ~0.5 s (partial)
        // to ~2.6 s (full). It runs on the Network task, so the watchdog is fed
        // immediately before and after, per the system-architecture
        // "blocking external call" requirement.
        const uint32_t start = millis();
        feedWatchdog();
        runPagedDraw(tempStr, humStr, footerName, footerDateTime, controlState, setpointStr,
                     demandSegments);
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

            // Full window here, so the band is painted rather than clipped:
            // the version is exactly what you want to be able to read off the
            // panel if the boot never completes.
            drawHeader();

            display.setFont(&FreeSans12pt7b);
            if (deviceName != nullptr && deviceName[0] != '\0') {
                // Centred, so it only has to fit between the margins — but
                // device_name allows 31 characters, which in this font can be
                // wider than the panel.
                char name[40];
                fitToWidth(deviceName, PANEL_W - 2 * FOOTER_MARGIN_X, name, sizeof(name));
                if (name[0] != '\0') {
                    drawCentered(name, SPLASH_NAME_Y);
                }
            }
            display.setFont(nullptr);
            drawCentered("starting...", SPLASH_STATUS_Y);
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
