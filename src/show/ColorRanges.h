#ifndef LEDZ_COLORRANGES_H
#define LEDZ_COLORRANGES_H

#include "Show.h"
#include "../support/SmoothBlend.h"
#include <memory>
#include <vector>

namespace Show {
    /**
     * ColorRanges - Displays color sections across the LED strip
     * Perfect for single solid colors, flags, multi-color patterns, and gradients
     * Supports 1 or more colors with optional gradient mode
     */
    class ColorRanges : public Show {
    private:
        std::vector<Strip::Color> colors; // List of colors to display
        std::vector<float> ranges; // Boundary percentages (0-100)
        bool gradient; // Gradient mode: smooth interpolation between colors
        std::unique_ptr<Support::SmoothBlend> blend;
        bool initialized = false;

    public:
        /**
         * Constructor with colors, optional ranges, and optional gradient mode
         * @param colors Vector of colors to display
         * @param ranges Vector of boundary percentages (0-100). If empty, colors distribute equally.
         *               Example: [30, 70] with 3 colors creates: color1 (0-30%), color2 (30-70%), color3 (70-100%)
         *               Note: ranges is ignored in gradient mode (colors are evenly distributed as waypoints)
         * @param gradient If true, colors act as waypoints with smooth interpolation between them.
         *                 If false (default), colors fill sections with sharp boundaries.
         */
        ColorRanges(const std::vector<Strip::Color> &colors,
                    const std::vector<float> &ranges = {},
                    bool gradient = false);

        /**
         * Execute the show - creates color ranges and smoothly blends to them
         * @param strip LED strip to control
         * @param iteration Current iteration number
         */
        void execute(Strip::Strip &strip, Iteration iteration) override;

        /**
         * Check if the show has reached its final static state
         * @return true if blend is complete
         */
        bool isComplete() const override;

        const char *name() { return "ColorRanges"; }
    };
} // namespace Show

#endif //LEDZ_COLORRANGES_H