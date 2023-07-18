#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>

#include "config.h"
#include "leds.h"
#include "net.h"
#include "serialap.h"
#include "settings.h"
#include "util.h"

void setup() {
    // starts the led task/state machine
    xTaskCreate(ledTask, "ledhandler", 2000, NULL, 2, NULL);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // show a nice pattern to indicate the AP is booting / waiting for WiFi setup
    showColorPattern(CRGB::Aqua, CRGB::Green, CRGB::Blue);

    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    Serial.setTimeout(5000);
    Serial.readStringUntil('\n');
    Serial.setTimeout(1000);
    logln(">\n");

#ifdef BOARD_HAS_PSRAM
    if (!psramInit()) {
        log("This build of the AP expects PSRAM, but we couldn't find/init any. Something is terribly wrong here! System halted.");
        showColorPattern(CRGB::Yellow, CRGB::Red, CRGB::Red);
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    };
    heap_caps_malloc_extmem_enable(64);
#endif

    logln("\n\n##################################");
    logf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
    logf("SPIRam Total heap %d, SPIRam Free Heap %d\n", ESP.getPsramSize(), ESP.getFreePsram());
    logf("ChipRevision %d, Cpu Freq %d, SDK Version %s\n", ESP.getChipRevision(), ESP.getCpuFreqMHz(), ESP.getSdkVersion());
    logf("Flash Size %d, Flash Speed %d\n", ESP.getFlashChipSize(), ESP.getFlashChipSpeed());
    logln("##################################\n\n");

    logf("Total heap: %d\n", ESP.getHeapSize());
    logf("Free heap: %d\n", ESP.getFreeHeap());
    logf("Total PSRAM: %d\n", ESP.getPsramSize());
    logf("Free PSRAM: %d\n\n", ESP.getFreePsram());

    logf("ESP32 Partition table:\n");
    logf("| Type | Sub |  Offset  |   Size   |       Label      |\n");
    logf("| ---- | --- | -------- | -------- | ---------------- |\n");
    esp_partition_iterator_t pi = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (pi != NULL) {
        do {
            const esp_partition_t* p = esp_partition_get(pi);
            logf("|  %02x  | %02x  | 0x%06X | 0x%06X | %-16s |\r\n",
                 p->type, p->subtype, p->address, p->size, p->label);
        } while (pi = (esp_partition_next(pi)));
    }

    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "0.nl.pool.ntp.org", "europe.pool.ntp.org", "time.nist.gov");
    // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

    LittleFS.begin(true);
    init_config();

    init_network();
    xTaskCreate(network_task, "Network Process", 6000, NULL, 2, NULL);
    xTaskCreate(APTask, "AP Loop", 6000, NULL, 5, NULL);

#ifdef HAS_RGB_LED
    rgbIdle();
#endif
    // xTaskCreate(APTask, "AP Process", 6000, NULL, 2, NULL);
}

void loop() {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
