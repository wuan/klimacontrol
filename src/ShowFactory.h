// ShowFactory - Manages show registration and creation
//

#ifndef LEDZ_SHOWFACTORY_H
#define LEDZ_SHOWFACTORY_H

#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <string>

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#endif

#include "show/Show.h"
#include "Config.h"

/**
 * ShowFactory
 * Factory pattern for creating LED shows by name
 * Supports show registration and parameter parsing
 */
class ShowFactory {
public:
    /**
     * Show constructor function type that takes JSON parameters
     */
    using ShowConstructor = std::function<std::unique_ptr<Show::Show>(const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &)>;

    /**
     * Show metadata for listing available shows
     */
    struct ShowInfo {
        std::string name;
        std::string description;
    };

private:
    std::map<std::string, ShowConstructor> showConstructors;
    std::vector<ShowInfo> showList;

    /**
     * Parse colors from JSON and ensure exactly n colors are returned
     * @param doc JSON document containing "colors" array
     * @param count Number of colors to return
     * @param defaultColor Default color if none provided
     * @return Vector with exactly n colors
     */
    static std::vector<uint32_t> parseColors(const StaticJsonDocument<Config::JSON_DOC_MEDIUM> &doc,
                                              size_t count,
                                              uint32_t defaultColor);

public:
    ShowFactory();

    // disable copy constructor
    ShowFactory(const ShowFactory &) = delete;

    /**
     * Register a show with the factory
     * @param name Show name (e.g., "Rainbow")
     * @param description Human-readable description
     * @param constructor Function that creates the show instance
     */
    void registerShow(const std::string &name, const std::string &description, ShowConstructor &&constructor);

    /**
     * Create a show by name
     * @param name Show name
     * @return Show instance (caller owns pointer) or nullptr if not found
     */
    std::unique_ptr<Show::Show> createShow(const std::string &name);

    /**
     * Create a show by name with JSON parameters
     * @param name Show name
     * @param paramsJson JSON string with parameters (e.g., {"r":255,"g":0,"b":0})
     * @return Show instance (caller owns pointer) or nullptr if not found
     */
    std::unique_ptr<Show::Show> createShow(const std::string &name, const std::string &paramsJson);

    /**
     * Get list of all registered shows
     * @return Vector of show names
     */
    [[nodiscard]] const std::vector<ShowInfo> &listShows() const;

    /**
     * Check if a show is registered
     * @param name Show name
     * @return true if registered
     */
    bool hasShow(const std::string &name) const;
};

#endif //LEDZ_SHOWFACTORY_H
