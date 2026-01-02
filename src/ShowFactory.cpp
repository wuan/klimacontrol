#include "ShowFactory.h"
#include "show/Rainbow.h"
#include "show/ColorRanges.h"
#include "show/ColorRun.h"
#include "show/Mandelbrot.h"
#include "show/Chaos.h"
#include "show/Jump.h"
#include "show/Starlight.h"
#include "show/Wave.h"
#include "show/MorseCode.h"
#include "show/TheaterChase.h"
#include "show/Stroboscope.h"
#include "show/Fire.h"
#include "color.h"

#ifdef ARDUINO
#include <ArduinoJson.h>
#endif

ShowFactory::ShowFactory() {
    // Register all available shows (in display order)
    // Each lambda receives a StaticJsonDocument and uses defaults via | operator

    registerShow("Solid", "Solid color or color sections (flags, patterns, gradients)", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        std::vector<Strip::Color> colors;
        std::vector<float> ranges;

        // Parse colors array (supports 1 or more colors)
        // Note: Must use JsonArrayConst for StaticJsonDocument (not JsonArray)
        if (doc.containsKey("colors")) {
            JsonArrayConst colorsArray = doc["colors"].as<JsonArrayConst>();
            if (!colorsArray.isNull() && colorsArray.size() > 0) {
                for (JsonVariantConst colorVariant: colorsArray) {
                    JsonArrayConst colorArray = colorVariant.as<JsonArrayConst>();
                    if (!colorArray.isNull() && colorArray.size() >= 3) {
                        uint8_t r = colorArray[0].as<uint8_t>();
                        uint8_t g = colorArray[1].as<uint8_t>();
                        uint8_t b = colorArray[2].as<uint8_t>();
                        colors.push_back(color(r, g, b));
                    }
                }
            }
        }

        // Parse ranges array (optional)
        if (doc.containsKey("ranges")) {
            JsonArrayConst rangesArray = doc["ranges"].as<JsonArrayConst>();
            if (!rangesArray.isNull() && rangesArray.size() > 0) {
                for (JsonVariantConst rangeVariant: rangesArray) {
                    ranges.push_back(rangeVariant.as<float>());
                }
            }
        }

        // Parse gradient flag (optional, default false)
        bool gradient = doc["gradient"] | false;

        // If no colors parsed, use default warm white
        if (colors.empty()) {
            colors.push_back(color(255, 250, 230)); // Warm white
        }

        return std::make_unique<Show::ColorRanges>(colors, ranges, gradient);
    });

    registerShow("Fire", "Burning flames", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        float cooling = doc["cooling"] | 0.1f;
        float spread = doc["spread"] | 10.0f;
        float ignition = doc["ignition"] | 0.5f;
        float spark_amount = doc["spark_amount"] | 0.5f;
        int start_offset = doc["start_offset"] | 5;
        int spark_range = doc["spark_range"] | 5;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating Fire cooling=%.2f, spread=%.2f, ignition=%.2f, spark_amount=%.2f, start_offset=%d, spark_range=%d\n",
                      cooling, spread, ignition, spark_amount, start_offset, spark_range);
#endif
        return std::make_unique<Show::Fire>(cooling, spread, ignition, spark_amount, std::vector<float>{1.0f}, start_offset, spark_range);
    });

    registerShow("Starlight", "Twinkling stars effect", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        float probability = doc["probability"] | 0.1f;
        unsigned long length_ms = doc["length"] | 5000;
        unsigned long fade_ms = doc["fade"] | 1000;
        uint8_t r = doc["r"] | 255;
        uint8_t g = doc["g"] | 180;
        uint8_t b = doc["b"] | 50;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating Starlight probability=%.2f, length=%lums, fade=%lums, RGB(%d,%d,%d)\n",
                      probability, length_ms, fade_ms, r, g, b);
#endif
        return std::make_unique<Show::Starlight>(probability, length_ms, fade_ms, r, g, b);
    });

    registerShow("Stroboscope", "Flashing strobe effect", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        uint8_t r = doc["r"] | 255;
        uint8_t g = doc["g"] | 255;
        uint8_t b = doc["b"] | 255;
        unsigned int on_cycles = doc["on_cycles"] | 1;
        unsigned int off_cycles = doc["off_cycles"] | 10;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating Stroboscope RGB(%d,%d,%d), on=%u, off=%u\n",
                      r, g, b, on_cycles, off_cycles);
#endif
        return std::make_unique<Show::Stroboscope>(r, g, b, on_cycles, off_cycles);
    });

    registerShow("ColorRun", "Running colors", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        // ColorRun has no parameters yet
        return std::make_unique<Show::ColorRun>();
    });

    registerShow("Jump", "Jumping lights", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        // Jump has no parameters yet
        return std::make_unique<Show::Jump>();
    });

    registerShow("Rainbow", "Rainbow color cycle", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        // Rainbow has no parameters yet
        Serial.println("ShowFactory: Creating Rainbow");

        return std::make_unique<Show::Rainbow>();
    });

    registerShow("Wave", "Propagating wave with rainbow colors", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        float wave_speed = doc["wave_speed"] | 1.0f;
        float decay_rate = doc["decay_rate"] | 2.0f;
        float brightness_frequency = doc["brightness_frequency"] | 0.1f;
        float wavelength = doc["wavelength"] | 6.0f;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating Wave speed=%.2f, decay=%.2f, freq=%.2f, wavelength=%.2f\n",
                      wave_speed, decay_rate, brightness_frequency, wavelength);
#endif
        return std::make_unique<Show::Wave>(wave_speed, decay_rate, brightness_frequency, wavelength);
    });

    registerShow("TheaterChase", "Marquee-style chase with rainbow colors", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        unsigned int num_steps_per_cycle = doc["num_steps_per_cycle"] | 21;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating TheaterChase num_steps_per_cycle=%u\n",
                      num_steps_per_cycle);
#endif
        return std::make_unique<Show::TheaterChase>(num_steps_per_cycle);
    });

    registerShow("MorseCode", "Scrolling Morse code text display", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
#ifdef ARDUINO
        String message = doc["message"] | "HELLO";
#else
        const char *message = "HELLO";
#endif
        float speed = doc["speed"] | 0.5f;
        unsigned int dot_length = doc["dot_length"] | 2;
        unsigned int dash_length = doc["dash_length"] | 4;
        unsigned int symbol_space = doc["symbol_space"] | 2;
        unsigned int letter_space = doc["letter_space"] | 3;
        unsigned int word_space = doc["word_space"] | 5;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating MorseCode message=\"%s\", speed=%.2f, dot=%u, dash=%u\n",
                      message.c_str(), speed, dot_length, dash_length);
#endif
        return std::make_unique<Show::MorseCode>(message.c_str(), speed, dot_length, dash_length,
                                                 symbol_space, letter_space, word_space);
    });

    registerShow("Chaos", "Chaotic pattern", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        float Rmin = doc["Rmin"] | 2.95f;
        float Rmax = doc["Rmax"] | 4.0f;
        float Rdelta = doc["Rdelta"] | 0.0002f;
#ifdef ARDUINO
        Serial.printf("ShowFactory: Creating Chaos Rmin=%.4f, Rmax=%.4f, Rdelta=%.6f\n",
                      Rmin, Rmax, Rdelta);
#endif
        return std::make_unique<Show::Chaos>(Rmin, Rmax, Rdelta);
    });

    registerShow("Mandelbrot", "Mandelbrot fractal zoom", [](const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc) {
        float Cre0 = doc["Cre0"] | -1.05f;
        float Cim0 = doc["Cim0"] | -0.3616f;
        float Cim1 = doc["Cim1"] | -0.3156f;
        unsigned int scale = doc["scale"] | 5;
        unsigned int max_iterations = doc["max_iterations"] | 50;
        unsigned int color_scale = doc["color_scale"] | 10;
#ifdef ARDUINO
        Serial.printf(
            "ShowFactory: Creating Mandelbrot Cre0=%.4f, Cim0=%.4f, Cim1=%.4f, scale=%u, max_iter=%u, color_scale=%u\n",
            Cre0, Cim0, Cim1, scale, max_iterations, color_scale);
#endif
        return std::make_unique<Show::Mandelbrot>(Cre0, Cim0, Cim1, scale, max_iterations, color_scale);
    });
}

void ShowFactory::registerShow(const std::string &name, const std::string &description, ShowConstructor &&constructor) {
    showConstructors[name] = std::move(constructor);
    showList.push_back({name, description});
}

std::unique_ptr<Show::Show> ShowFactory::createShow(const std::string &name) {
    return createShow(name, "{}"); // Default to empty params
}

std::unique_ptr<Show::Show> ShowFactory::createShow(const std::string &name, const std::string &paramsJson) {
    // Check if show exists
    auto it = showConstructors.find(name);
    if (it == showConstructors.end()) {
#ifdef ARDUINO
        Serial.printf("ShowFactory: show %s not found", name.c_str());
#endif
        return {};
    }

#ifdef ARDUINO
    // Parse JSON parameters (increased buffer for ColorRanges with many colors and nested arrays)
    StaticJsonDocument<Config::JSON_DOC_LARGE> doc;
    DeserializationError error = deserializeJson(doc, paramsJson.c_str());

    // If JSON parsing fails, log warning and use empty document (will use defaults)
    if (error) {
        Serial.print("ShowFactory: Failed to parse params for ");
        Serial.print(name.c_str());
        Serial.print(": ");
        Serial.println(error.c_str());
        Serial.println("Using default parameters");
        doc.clear(); // Empty document will trigger all defaults via | operator
    }

    // Call the stored factory function with the parsed (or empty) JSON document
    return it->second(doc);
#else
    // Non-Arduino builds: use empty document (defaults)
    StaticJsonDocument<Config::JSON_DOC_MEDIUM> doc;
    return std::move(it->second(doc));
#endif
}

const std::vector<ShowFactory::ShowInfo> &ShowFactory::listShows() const {
    return showList;
}

bool ShowFactory::hasShow(const std::string &name) const {
    return showConstructors.find(name) != showConstructors.end();
}
