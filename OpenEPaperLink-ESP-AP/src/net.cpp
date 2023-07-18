#include "net.h"

#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "leds.h"
#include "processing.h"
#include "util.h"

static WiFiClient espClient;
static PubSubClient mqtt(espClient);
static AsyncWebServer server(80);
static SemaphoreHandle_t mqtt_semaphore;

bool shouldReboot = false;  // flag to use from web update to reboot the ESP

static void on_message(char* topic, uint8_t* payload, uint32_t length);

class MqttLock {
   public:
    bool success;
    MqttLock(uint32_t timeout_ms = 100) {
        success = xSemaphoreTake(mqtt_semaphore, timeout_ms * portTICK_PERIOD_MS);
    }

    ~MqttLock() {
        if (success) {
            xSemaphoreGive(mqtt_semaphore);
        }
    }
};

void init_network() {
    mqtt_semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(mqtt_semaphore);

    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    bool res;
    res = wm.autoConnect("OpenEPaperLink Setup");
    if (!res) {
        logln("Failed to connect");
        ESP.restart();
    }
    log("Connected! IP address: ");
    logln(WiFi.localIP());

    if (!mqtt.setBufferSize(MQTT_BUFFER_SIZE)) {
        logln("panic! alloc for MQTT buffer failed!");
        showColorPattern(CRGB::Yellow, CRGB::Red, CRGB::Red);
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    mqtt.setServer(config.mqtt_broker.c_str(), 1883);
    mqtt.setCallback(&on_message);

    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "OK Reboot");
        logln("REBOOTING");
        delay(100);
        ESP.restart();
    });

    server.on(
        "/config", HTTP_PUT, [](AsyncWebServerRequest* request) {
        request->send(204);
        init_config(); }, NULL, [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        fs::File file = LittleFS.open(CONFIG_LOCATION, index == 0 ? "w" : "a");
        if (!file) {
            logln("PUT /config: Failed to open file");
            return;
        }
        file.write(data, len);
        file.close(); });

    // Simple Firmware Update Form
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>"); });
    server.on(
        "/update", HTTP_POST, [](AsyncWebServerRequest* request) {
    shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response); },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                Serial.printf("Update Start: %s\n", filename.c_str());
                // Update.runAsync(true);
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("Update Success: %uB\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        });
}

static void on_message(char* topic, uint8_t* payload, uint32_t length) {
    auto topicString = String(topic);
    if (topicString == "/tag/sda") {
        process_mqtt_sda(payload, length);
    }
}

void mqtt_keepalive() {
    MqttLock lock(1000);
    if (!lock.success) {
        logln("mqtt_keepalive: unable to lock in 1000ms");
        return;
    }
    if (!mqtt.connected()) {
        mqtt.connect(config.mqtt_id.c_str(), config.mqtt_username.c_str(), config.mqtt_password.c_str());
    }
    mqtt.loop();
}

void network_task(void* parameter) {
    server.begin();
    mqtt_keepalive();
    mqtt.subscribe("/tag/sda");
    while (1) {
        // Reboot if needed
        if (shouldReboot) {
            Serial.println("Rebooting...");
            delay(100);
            ESP.restart();
        }
        mqtt_keepalive();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void mqtt_send_message(const char* topic, const char* payload, uint32_t length) {
    MqttLock lock;
    if (!lock.success) {
        log("Dropping message to ");
        log(topic);
        logln(" due to lock congestion");
    } else {
        mqtt.publish(topic, payload, length);
    }
}