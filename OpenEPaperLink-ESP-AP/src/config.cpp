#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "leds.h"
#include "util.h"

Config config;

static String get_string(const JsonDocument &doc, String attr);

void init_config() {
    if (!LittleFS.exists(CONFIG_LOCATION)) {
        fs:File f = LittleFS.open(CONFIG_LOCATION, "w", true);
        if (!f) {
            logln("panic! initial config write failed!");
            showColorPattern(CRGB::Yellow, CRGB::Red, CRGB::Red);
            while (1) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
        const uint8_t* emptyConfig = (const uint8_t*)(const char*)"{}";
        f.write(emptyConfig, 2);
        f.close();
    }

    StaticJsonDocument<1000> doc;
    fs::File readfile = LittleFS.open(CONFIG_LOCATION, "r");
    if (!readfile) {
        logln("panic! config open failed");
        showColorPattern(CRGB::Yellow, CRGB::Red, CRGB::Red);
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    DeserializationError err = deserializeJson(doc, readfile);
    if (err) {
        log(F("deserializeJson() failed: "));
        logln(err.c_str());
        // maybe config is invalid. continue to allow writing of new config
        return;
    }
    config.mqtt_broker = get_string(doc, "mqtt_broker");
    config.mqtt_id = get_string(doc, "mqtt_id");
    config.mqtt_username = get_string(doc, "mqtt_username");
    config.mqtt_password = get_string(doc, "mqtt_password");
}

void save_config() {
    DynamicJsonDocument doc(1000);
    fs::File file = LittleFS.open(CONFIG_LOCATION, "w");
    if (!file) {
        logln("save_config: Failed to open file");
        return;
    }
    auto root = doc.createNestedObject();
    root["mqtt_broker"] = config.mqtt_broker;
    root["mqtt_id"] = config.mqtt_id;
    root["mqtt_username"] = config.mqtt_username;
    root["mqtt_password"] = config.mqtt_password;
    serializeJson(root, file);
    file.close();
}

static String get_string(const JsonDocument &doc, String attr) {
    auto value = doc[attr];
    if (!value.isNull()) {
        log("Parsed ");
        log(attr);
        log(" as ");
        logln(value.as<String>());
        return value.as<String>();
    } else {
        log(attr);
        logln(" is absent");
        return "";
    }
}