#include "processing.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <map>
#include <memory>

#include "config.h"
#include "net.h"
#include "serialap.h"
#include "util.h"

#define MAX_PENDING_TAGS 100

class PendingTag {
   public:
    pendingData pd;
    std::unique_ptr<char[]> data;
    PendingTag(pendingData pd, const char* data) {
        this->pd = pd;
        this->data = std::unique_ptr<char[]>(new char[pd.availdatainfo.dataSize]);
        memcpy(this->data.get(), data, pd.availdatainfo.dataSize);
    }
};

std::map<uint64_t, PendingTag> pendingTags;

void processBlockRequest(struct espBlockRequest* br) {
    DynamicJsonDocument doc(1000);
    auto root = doc.createNestedObject();
    uint64_t swapped = swap64(*(uint64_t*)br->src);
    root["type"] = "RQB";
    root["src"] = hex_8u8((uint8_t*)&swapped);
    root["version"] = hex_8u8_plain((uint8_t*)&br->ver);
    root["block_id"] = br->blockId;
    String json;
    serializeJson(root, json);
    mqtt_send_message("/tag/rqb", json.c_str(), json.length());
    const auto& entry = pendingTags.find(cast8u8_u64(br->src));
    if (entry == pendingTags.end()) {
        log("Cancelling RQB for unknown tag ");
        logln(root["src"].as<String>());
        pendingData cancelPd;
        memcpy(cancelPd.targetMac, br->src, 8);
        sendCancelPending(&cancelPd);
        return;
    }
    const auto& tag = entry->second;
    uint32_t offset = br->blockId * BLOCK_DATA_SIZE;
    uint32_t len = std::min<uint32_t>(tag.pd.availdatainfo.dataSize - offset, BLOCK_DATA_SIZE);
    logf("Sending block %d of version %s with %d bytes\n", br->blockId, root["version"].as<String>().c_str(), len);
    sendBlock(tag.data.get() + offset, len);
};

void processXferComplete(struct espXferComplete* xfc, bool local) {
    const auto& entry = pendingTags.find(cast8u8_u64(xfc->src));
    if (entry != pendingTags.end()) {
        pendingTags.erase(entry);
    }

    DynamicJsonDocument doc(200);
    auto root = doc.createNestedObject();
    uint64_t swapped = swap64(*(uint64_t*)xfc->src);
    root["type"] = "XFC";
    root["src"] = hex_8u8((uint8_t*)&swapped);
    String json;
    serializeJson(root, json);
    mqtt_send_message("/tag/xfc", json.c_str(), json.length());
};

void processXferTimeout(struct espXferComplete* xfc, bool local) {
    DynamicJsonDocument doc(200);
    auto root = doc.createNestedObject();
    uint64_t swapped = swap64(*(uint64_t*)xfc->src);
    root["type"] = "XTO";
    root["src"] = hex_8u8((uint8_t*)&swapped);
    String json;
    serializeJson(root, json);
    mqtt_send_message("/tag/xto", json.c_str(), json.length());
};

void processDataReq(struct espAvailDataReq* adr, bool local) {
    DynamicJsonDocument doc(1000);
    auto root = doc.createNestedObject();
    uint64_t swapped = swap64(*(uint64_t*)adr->src);
    root["type"] = "ADR";
    root["src"] = hex_8u8((uint8_t*)&swapped);
    root["last_lqi"] = adr->adr.lastPacketLQI;
    root["last_rssi"] = adr->adr.lastPacketRSSI;
    root["temperature"] = adr->adr.temperature;
    root["battery_mv"] = adr->adr.batteryMv;
    root["mqtt_id"] = config.mqtt_id;
    String json;
    serializeJson(root, json);
    mqtt_send_message("/tag/adr", json.c_str(), json.length());
    const auto& entry = pendingTags.find(cast8u8_u64(adr->src));
    if (entry != pendingTags.end()) {
        sendDataAvail(&entry->second.pd);
    }
};

void process_mqtt_sda(uint8_t* payload, uint32_t length) {
    if (length < sizeof(TagSDA)) {
        log("invalid SDA of length ");
        log(length);
        logln(", which is too short");
        return;
    }
    auto sda = (TagSDA*)payload;
    if (sda->sda.dataSize != length - sizeof(TagSDA)) {
        log("invalid data size in SDA of length ");
        log(length);
        log(", which should contain ");
        log(sda->sda.dataSize);
        log(" bytes of data, but has ");
        logln(length - sizeof(TagSDA));
        return;
    }

    const auto& entry = pendingTags.find(cast8u8_u64(sda->mac));
    if (entry == pendingTags.end() && pendingTags.size() >= MAX_PENDING_TAGS) {
        // todo: find a better tag to remove than the "first one"
        pendingTags.erase(pendingTags.begin());
        logln("pending tag overflow");
    }

    pendingData pending;
    pending.availdatainfo = sda->sda;
    pending.attemptsLeft = 60 * 24;
    memcpy(pending.targetMac, sda->mac, 8);

    if (entry != pendingTags.end()) {
        pendingTags.erase(entry);
        // insert_or_assing now available yet
    };
    pendingTags.insert(std::make_pair(cast8u8_u64(sda->mac), PendingTag(pending, (const char*)payload + sizeof(TagSDA))));
    uint64_t swapped = swap64(*(uint64_t*)sda->mac);
    log("Received version ");
    log(hex_8u8_plain((const uint8_t*)&sda->sda.dataVer));
    log(" for tag ");
    logln(hex_8u8((uint8_t*)&swapped));
    sendDataAvail(&pending);
}