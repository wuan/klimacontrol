#ifndef KLIMACONTROL_DISPLAY_DISPLAYFIELD_H
#define KLIMACONTROL_DISPLAY_DISPLAYFIELD_H

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Config.h"
#include "sensor/Sensor.h"

// Pure, native-testable metadata and formatting for the configurable display
// value slots. Kept free of Arduino/GxEPD2 so the mapping, variant extraction,
// placeholder formatting and hysteresis can be unit-tested on the host.
//
// Width budget (FreeSans12pt7b): the widest configured line is
// "PM2.5 8 ug/m3", measured at ~155 px on the 200 px panel, with margins. The
// longest warning label, "OVERHEAT", is 8 glyphs at the built-in font's 6 px
// advance = 48 px and fits the margin slot (centres x=31 and x=169). A future
// field that does not fit is a spec/design extension, not a silent truncation.
namespace Display {

    using DisplayField = Config::DisplayField;

    struct FieldSpec {
        DisplayField field;
        Sensor::MeasurementType measurement;
        const char* label; // prefix before the number, e.g. "DP", "P", "PM2.5"
        const char* unit;  // suffix after the number, e.g. "hPa", "ug/m3"
        uint8_t decimals;
        float hysteresis;
        bool degreeRing; // true when a degree ring is drawn after the text
    };

    struct FieldReading {
        float value = NAN;
        bool available = false;
    };

    /**
     * Metadata for a configured field, or nullptr for None / an invalid value.
     */
    const FieldSpec* fieldSpec(DisplayField field);

    /**
     * Field-specific refresh hysteresis, or 0 for None / an invalid value.
     */
    float fieldHysteresis(DisplayField field);

    /**
     * Whether the rendered line needs a drawn degree ring (dew point).
     */
    bool fieldNeedsDegreeRing(DisplayField field);

    /**
     * Extract a numeric value from a measurement's float/int32 variant.
     * Returns false when the variant holds neither type. The extracted value
     * may still be NAN; availability checks filter that separately.
     */
    bool measurementValueAsFloat(const Sensor::Measurement& measurement, float& out);

    /**
     * Read a field from an already-captured snapshot. A reading is available
     * only when the snapshot is valid, the measurement exists, its value is a
     * finite float/int32, and the field's extra dependency is satisfied:
     * sea-level pressure needs a configured elevation > 0, and VOC index needs
     * temperature and humidity for SGP40 compensation.
     */
    FieldReading readDisplayField(DisplayField field, const std::vector<Sensor::Measurement>& measurements,
                                  bool snapshotValid, float elevation);

    /**
     * Format the full rendered line, including label and unit. Placeholders
     * keep the label and unit and replace only the numeric part:
     * "P -- hPa", "DP --", "CO2 -- ppm", "PM2.5 -- ug/m3", "-- %rH".
     * Returns the number of characters written.
     */
    size_t formatDisplayFieldLine(char* out, size_t n, DisplayField field, const FieldReading& reading);

} // namespace Display

#endif // KLIMACONTROL_DISPLAY_DISPLAYFIELD_H
