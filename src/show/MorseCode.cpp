#include "MorseCode.h"
#include "../color.h"
#include <algorithm>
#include <cctype>

namespace Show {
    // International Morse Code dictionary
    const char *MorseCode::getMorseCode(char c) {
        // Convert to uppercase
        c = toupper(c);

        // Letters
        if (c == 'A') return ".-";
        if (c == 'B') return "-...";
        if (c == 'C') return "-.-.";
        if (c == 'D') return "-..";
        if (c == 'E') return ".";
        if (c == 'F') return "..-.";
        if (c == 'G') return "--.";
        if (c == 'H') return "....";
        if (c == 'I') return "..";
        if (c == 'J') return ".---";
        if (c == 'K') return "-.-";
        if (c == 'L') return ".-..";
        if (c == 'M') return "--";
        if (c == 'N') return "-.";
        if (c == 'O') return "---";
        if (c == 'P') return ".--.";
        if (c == 'Q') return "--.-";
        if (c == 'R') return ".-.";
        if (c == 'S') return "...";
        if (c == 'T') return "-";
        if (c == 'U') return "..-";
        if (c == 'V') return "...-";
        if (c == 'W') return ".--";
        if (c == 'X') return "-..-";
        if (c == 'Y') return "-.--";
        if (c == 'Z') return "--..";

        // Numbers
        if (c == '0') return "-----";
        if (c == '1') return ".----";
        if (c == '2') return "..---";
        if (c == '3') return "...--";
        if (c == '4') return "....-";
        if (c == '5') return ".....";
        if (c == '6') return "-....";
        if (c == '7') return "--...";
        if (c == '8') return "---..";
        if (c == '9') return "----.";

        // Common punctuation
        if (c == '.') return ".-.-.-";
        if (c == ',') return "--..--";
        if (c == '?') return "..--..";
        if (c == '!') return "-.-.--";
        if (c == '-') return "-....-";
        if (c == '/') return "-..-.";
        if (c == '@') return ".--.-.";

        // Unknown character - return empty
        return "";
    }

    void MorseCode::buildPattern() {
        pattern.clear();

        // Split message into words
        std::vector<std::string> words;
        std::string current_word;

        for (char c: message) {
            if (c == ' ') {
                if (!current_word.empty()) {
                    words.push_back(current_word);
                    current_word.clear();
                }
            } else {
                current_word += c;
            }
        }
        if (!current_word.empty()) {
            words.push_back(current_word);
        }

        // If no words, use default message
        if (words.empty()) {
            words.push_back("HELLO");
        }

        // Encode each word with a unique color
        for (size_t word_idx = 0; word_idx < words.size(); word_idx++) {
            // Assign color from wheel based on word index
            uint8_t color_index = (uint8_t) ((word_idx * 255) / std::max(1, (int) words.size()));
            Strip::Color word_color = wheel(color_index);

            const std::string &word = words[word_idx];

            // Encode each letter in the word
            for (size_t char_idx = 0; char_idx < word.length(); char_idx++) {
                const char *morse = getMorseCode(word[char_idx]);

                // Skip unknown characters
                if (morse[0] == '\0') continue;

                // Encode each symbol (dot or dash)
                for (size_t symbol_idx = 0; morse[symbol_idx] != '\0'; symbol_idx++) {
                    // Add the symbol LEDs
                    unsigned int symbol_len = (morse[symbol_idx] == '.') ? dot_length : dash_length;
                    for (unsigned int i = 0; i < symbol_len; i++) {
                        pattern.push_back(word_color);
                    }

                    // Add symbol space (except after last symbol in letter)
                    if (morse[symbol_idx + 1] != '\0') {
                        for (unsigned int i = 0; i < symbol_space; i++) {
                            pattern.push_back(color(0, 0, 0));
                        }
                    }
                }

                // Add letter space (except after last letter in word)
                if (char_idx < word.length() - 1) {
                    for (unsigned int i = 0; i < letter_space; i++) {
                        pattern.push_back(color(0, 0, 0));
                    }
                }
            }

            // Add word space (except after last word)
            if (word_idx < words.size() - 1) {
                for (unsigned int i = 0; i < word_space; i++) {
                    pattern.push_back(color(0, 0, 0));
                }
            }
        }

        // Ensure pattern is not empty
        if (pattern.empty()) {
            pattern.push_back(color(255, 255, 255));
        }
    }

    MorseCode::MorseCode(const std::string &message, float speed,
                         unsigned int dot_length, unsigned int dash_length,
                         unsigned int symbol_space, unsigned int letter_space,
                         unsigned int word_space)
        : message(message), speed(speed), dot_length(dot_length),
          dash_length(dash_length), symbol_space(symbol_space),
          letter_space(letter_space), word_space(word_space), index(0) {
        // Convert message to uppercase
        std::transform(this->message.begin(), this->message.end(),
                       this->message.begin(), ::toupper);

        // Build the pattern
        buildPattern();
    }

    void MorseCode::execute(Strip::Strip &strip, Iteration iteration) {
        uint16_t num_leds = strip.length();
        unsigned int pattern_length = pattern.size();

        // Calculate scroll offset
        unsigned int offset = (unsigned int) (index * speed) % pattern_length;

        // Map pattern to strip with scrolling
        for (uint16_t i = 0; i < num_leds; i++) {
            unsigned int pattern_idx = (offset + i) % pattern_length;
            strip.setPixelColor(i, pattern[pattern_idx]);
        }

        // Increment index for next frame
        index++;
    }
} // namespace Show
