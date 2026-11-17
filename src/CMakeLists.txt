# The project source tree is registered as an IDF component. PlatformIO
# compiles these files itself, but CMake needs the component description to
# build the link line, resolve the Arduino component, and expose the headers.
set(srcs
    CaptivePortal.cpp
    Config.cpp
    DarkModeStatusLed.cpp
    I2CScanner.cpp
    MqttClient.cpp
    board_variant.cpp
    network/Network.cpp
    SensorController.cpp
    StatusLed.cpp
    SyslogOutput.cpp
    WebServerManager.cpp
    main.cpp

    actuator/HeatingActuator.cpp
    actuator/ShellyChannel.cpp

    control/PidController.cpp
    control/RelayAutotuner.cpp
    control/TemperatureController.cpp
    control/TimeProportionalOutput.cpp

    display/DisplayManager.cpp
    display/EPaperDisplay.cpp
    display/RefreshPolicy.cpp
    display/WarningPolicy.cpp

    network/ApProvisioning.cpp
    network/InternetHealth.cpp
    network/LinkMonitor.cpp
    network/LowHeapGuard.cpp
    network/MdnsAdvertiser.cpp
    network/MqttPayload.cpp
    network/MqttPublisher.cpp
    network/NtpSync.cpp
    network/WifiStation.cpp

    ota/OTAUpdater.cpp
    ota/http/HttpClient.cpp
    ota/http/HttpEventHandler.cpp
    ota/http/HttpReader.cpp
    ota/http/TlsAllocator.cpp

    routes/ControlRoutes.cpp
    routes/DisplayRoutes.cpp
    routes/I2CRoutes.cpp
    routes/MqttRoutes.cpp
    routes/OTARoutes.cpp
    routes/PageRoutes.cpp
    routes/SensorRoutes.cpp
    routes/SettingsRoutes.cpp
    routes/StatusRoutes.cpp
    routes/SyslogRoutes.cpp

    sensor/BH1750.cpp
    sensor/BME680.cpp
    sensor/BMP3xx.cpp
    sensor/DPS310.cpp
    sensor/DeviceSensor.cpp
    sensor/I2CSensor.cpp
    sensor/PM25.cpp
    sensor/SCD4x.cpp
    sensor/SGP40.cpp
    sensor/SHT4x.cpp
    sensor/TSL2591.cpp
    sensor/VEML7700.cpp

    support/LocalTime.cpp
    support/NetworkWatchdog.cpp
    support/RequestDiag.cpp
    support/Stats.cpp
    support/Timer.cpp
    support/WifiBackoff.cpp

    task/SensorMonitor.cpp
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        arduino_tinyusb
        esp_http_client
        esp_pm
        esp_system
        esp_timer
        espcoredump
        freertos
        heap
        nvs_flash
)
