// Host-side display renderer: drives the firmware's own EPaperDisplay drawing
// code (see scripts/render_display_mock.py) and dumps each rendered frame as
// a 1-bit PBM for the Python script to convert to PNG.

#include <cstdint>
#include <cstdio>
#include <string>

#include "GxEPD2_BW.h"
#include "display/EPaperDisplay.h"

namespace {

    struct Sample {
        const char* name;
        Display::WarningToken warning;
        const char* temp;
        const char* hum;
        Display::ControlState control;
        const char* setpoint;
        uint8_t demandSegments;
    };

    constexpr Sample SAMPLES[] = {
        {"normal", Display::WarningToken::NONE, "21.4", "45", Display::ControlState::ACTIVE_ON, "22.0", 3},
        {"frost", Display::WarningToken::FROST, "2.8", "52", Display::ControlState::ACTIVE_ON, "22.0", 3},
        {"overheat", Display::WarningToken::OVERHEAT, "31.6", "40", Display::ControlState::ACTIVE_ON, "22.0", 3},
        {"sensor", Display::WarningToken::SENSOR, "--.-", "--", Display::ControlState::ACTIVE_ON, "22.0", 3},
        {"actuator", Display::WarningToken::ACTUATOR, "21.4", "45", Display::ControlState::UNCERTAIN, "22.0", 0},
        {"humid", Display::WarningToken::HUMID, "21.4", "78", Display::ControlState::ACTIVE_ON, "22.0", 3},
    };

    std::string outDir;
    std::string currentName;

    void writePbm(const char* path, const uint8_t* fb, int16_t w, int16_t h) {
        FILE* f = fopen(path, "wb");
        if (f == nullptr) {
            fprintf(stderr, "render_host: cannot open %s\n", path);
            return;
        }
        fprintf(f, "P4\n%d %d\n", w, h);
        for (int16_t y = 0; y < h; ++y) {
            for (int16_t x = 0; x < w; x += 8) {
                uint8_t byte = 0;
                for (int bit = 0; bit < 8 && x + bit < w; ++bit) {
                    if (fb[static_cast<size_t>(y) * w + x + bit] == 0) {
                        byte |= static_cast<uint8_t>(0x80 >> bit); // PBM: 1 = black
                    }
                }
                fputc(byte, f);
            }
        }
        fclose(f);
    }

} // namespace

void render_host_frame(const uint8_t* fb, int16_t w, int16_t h) {
    const std::string path = outDir + "/display_" + currentName + ".pbm";
    writePbm(path.c_str(), fb, w, h);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT_DIR\n", argv[0]);
        return 2;
    }
    outDir = argv[1];

    Display::EPaperDisplay panel;
    if (!panel.begin(0)) {
        fprintf(stderr, "render_host: panel.begin() failed\n");
        return 1;
    }

    for (const auto& s : SAMPLES) {
        currentName = s.name;
        panel.render(s.temp, s.hum, "klima-aabbcc", "26-10-05 14:07", s.control, s.setpoint, s.demandSegments,
                     s.warning, Display::RefreshKind::Full);
    }
    return 0;
}
