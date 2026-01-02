#ifndef LEDZ_MORSECODE_H
#define LEDZ_MORSECODE_H

#include "Show.h"
#include <vector>
#include <string>

namespace Show {
    /**
     * MorseCode - Scrolling International Morse Code text display
     * Encodes text as dots and dashes with color-coded words
     */
    class MorseCode : public Show {
    private:
        std::string message;
        float speed; // Scrolling speed (LEDs per frame)
        unsigned int dot_length; // Length of a dot in LEDs
        unsigned int dash_length; // Length of a dash in LEDs
        unsigned int symbol_space; // Space between dots/dashes within letters
        unsigned int letter_space; // Space between letters
        unsigned int word_space; // Space between words

        std::vector<Strip::Color> pattern; // Precomputed color pattern
        unsigned int index; // Current frame index

        // Morse code encoding
        void buildPattern();

        const char *getMorseCode(char c);

    public:
        /**
         * Constructor with configurable parameters
         * @param message Text to display (will be converted to uppercase)
         * @param speed Scrolling speed in LEDs per frame (default: 0.5)
         * @param dot_length Length of a dot in LEDs (default: 2)
         * @param dash_length Length of a dash in LEDs (default: 4)
         * @param symbol_space Space between symbols within letters (default: 2)
         * @param letter_space Space between letters (default: 3)
         * @param word_space Space between words (default: 5)
         */
        MorseCode(const std::string &message = "HELLO WORLD!",
                  float speed = 0.2f,
                  unsigned int dot_length = 1,
                  unsigned int dash_length = 3,
                  unsigned int symbol_space = 2,
                  unsigned int letter_space = 3,
                  unsigned int word_space = 5);

        /**
         * Execute the show - update scrolling morse code animation
         * @param strip LED strip to control
         * @param iteration Current iteration number
         */
        void execute(Strip::Strip &strip, Iteration iteration) override;

        const char *name() { return "MorseCode"; }
    };
} // namespace Show

#endif //LEDZ_MORSECODE_H