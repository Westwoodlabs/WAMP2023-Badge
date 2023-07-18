#include "serialap.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <LittleFS.h>

#include "commstructs.h"
#include "leds.h"
#include "net.h"
#include "processing.h"
#include "settings.h"
#include "util.h"

QueueHandle_t rxCmdQueue;
SemaphoreHandle_t txActive;

// If a command is sent, it will wait for a reply here
#define CMD_REPLY_WAIT 0x00
#define CMD_REPLY_ACK 0x01
#define CMD_REPLY_NOK 0x02
#define CMD_REPLY_NOQ 0x03
volatile uint8_t cmdReplyValue = CMD_REPLY_WAIT;

#define AP_SERIAL_PORT Serial1

uint8_t channelList[6];
struct espSetChannelPower curChannel = {0, 11, 10};

#define RX_CMD_RQB 0x01
#define RX_CMD_ADR 0x02
#define RX_CMD_XFC 0x03
#define RX_CMD_XTO 0x04
#define RX_CMD_RDY 0x05
#define RX_CMD_RSET 0x06

#define AP_ACTIVITY_MAX_INTERVAL 30 * 1000
volatile uint32_t lastAPActivity = 0;
struct APInfoS apInfo;

struct rxCmd {
    uint8_t* data;
    uint8_t len;
    uint8_t type;
};

#define ZBS_RX_WAIT_HEADER 0
#define ZBS_RX_WAIT_PKT_LEN 1
#define ZBS_RX_WAIT_PKT_RX 2
#define ZBS_RX_WAIT_SEP1 3
#define ZBS_RX_WAIT_SEP2 4
#define ZBS_RX_WAIT_VER 6
#define ZBS_RX_BLOCK_REQUEST 7
#define ZBS_RX_WAIT_XFERCOMPLETE 8
#define ZBS_RX_WAIT_DATA_REQ 9
#define ZBS_RX_WAIT_JOINNETWORK 10
#define ZBS_RX_WAIT_XFERTIMEOUT 11
#define ZBS_RX_WAIT_MAC 12
#define ZBS_RX_WAIT_CHANNEL 13
#define ZBS_RX_WAIT_POWER 14
#define ZBS_RX_WAIT_PENDING 15
#define ZBS_RX_WAIT_NOP 16
#define ZBS_RX_WAIT_TYPE 17

void addCRC(void* p, uint8_t len) {
    uint8_t total = 0;
    for (uint8_t c = 1; c < len; c++) {
        total += ((uint8_t*)p)[c];
    }
    ((uint8_t*)p)[0] = total;
    // pr("%d",total);
}

void send_ap_info() {
    DynamicJsonDocument doc(1000);
    auto root = doc.createNestedObject();
    uint64_t swapped = swap64(*(uint64_t*)apInfo.mac);
    root["type"] = "AP_INFO";
    root["is_online"] = apInfo.isOnline;
    root["mac"] = hex_8u8((uint8_t*)&swapped);
    root["version"] = apInfo.version;
    root["channel"] = apInfo.channel;
    root["power"] = apInfo.power;
    root["pendingTags"] = apInfo.pending;
    root["noUpdateTags"] = apInfo.nop;
    String json;
    serializeJson(root, json);
    mqtt_send_message("/ap", json.c_str(), json.length());
}

bool txStart() {
    while (1) {
        if (xPortInIsrContext()) {
            if (xSemaphoreTakeFromISR(txActive, NULL) == pdTRUE) return true;
        } else {
            if (xSemaphoreTake(txActive, portTICK_PERIOD_MS)) return true;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        logln("wait... tx busy");
    }
    // this never happens. Should we make a timeout?
    return false;
}
void txEnd() {
    if (xPortInIsrContext()) {
        xSemaphoreGiveFromISR(txActive, NULL);
    } else {
        xSemaphoreGive(txActive);
    }
}
bool waitCmdReply() {
    uint32_t val = millis();
    while (millis() < val + 100) {
        switch (cmdReplyValue) {
            case CMD_REPLY_WAIT:
                break;
            case CMD_REPLY_ACK:
                lastAPActivity = millis();
                return true;
                break;
            case CMD_REPLY_NOK:
                log("NOK: ");
                lastAPActivity = millis();
                return false;
                break;
            case CMD_REPLY_NOQ:
                log("NOQ: ");
                lastAPActivity = millis();
                return false;
                break;
        }
        vTaskDelay(1 / portTICK_RATE_MS);
    }
    log("Timeout: ");
    return false;
}

#if (AP_PROCESS_PORT == FLASHER_AP_PORT)
#define AP_RESET_PIN FLASHER_AP_RESET
#define AP_POWER_PIN FLASHER_AP_POWER
#endif
#ifdef OPENEPAPERLINK_PCB
#if (AP_PROCESS_PORT == FLASHER_EXT_PORT)
#define AP_RESET_PIN FLASHER_EXT_RESET
#define AP_POWER_PIN FLASHER_EXT_POWER
#endif
#if (AP_PROCESS_PORT == FLASHER_ALTRADIO_PORT)
#define AP_RESET_PIN FLASHER_ALT_RESET
#define AP_POWER_PIN FLASHER_ALT_POWER
#endif
#endif

// Send data to the AP
uint16_t sendBlock(const void* data, const uint16_t len) {
    if (!apInfo.isOnline) return false;
    if (!txStart()) return 0;
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        cmdReplyValue = CMD_REPLY_WAIT;
        AP_SERIAL_PORT.print(">D>");
        if (waitCmdReply()) goto blksend;
        logf("block send failed in try %d\n", attempt);
    }
    log("Failed sending block...\n");
    txEnd();
    return 0;
blksend:
    uint8_t blockbuffer[sizeof(struct blockData)];
    struct blockData* bd = (struct blockData*)blockbuffer;
    bd->size = len;
    bd->checksum = 0;

    // calculate checksum
    for (uint16_t c = 0; c < len; c++) {
        bd->checksum += ((uint8_t*)data)[c];
    }

    // send blockData header
    for (uint8_t c = 0; c < sizeof(struct blockData); c++) {
        AP_SERIAL_PORT.write(0xAA ^ blockbuffer[c]);
    }

    // send an entire block of data
    uint16_t c;
    for (c = 0; c < len; c++) {
        AP_SERIAL_PORT.write(0xAA ^ ((uint8_t*)data)[c]);
    }

    // fill the rest of the block-length filled with something else (will end up as 0xFF in the buffer)
    for (; c < BLOCK_DATA_SIZE; c++) {
        AP_SERIAL_PORT.write(0x55);
    }

    // dummy bytes in case some bytes were missed, makes sure the AP gets kicked out of data-loading mode
    for (c = 0; c < 32; c++) {
        AP_SERIAL_PORT.write(0xF5);
    }
    delay(10);
    txEnd();
    return bd->checksum;
}
bool sendDataAvail(struct pendingData* pending) {
    if (!apInfo.isOnline) return false;
    if (!txStart()) return false;
    addCRC(pending, sizeof(struct pendingData));
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        cmdReplyValue = CMD_REPLY_WAIT;
        AP_SERIAL_PORT.print("SDA>");
        for (uint8_t c = 0; c < sizeof(struct pendingData); c++) {
            AP_SERIAL_PORT.write(((uint8_t*)pending)[c]);
        }
        if (waitCmdReply()) goto sdasend;
        logf("SDA send failed in try %d\n", attempt);
    }
    log("SDA failed to send...\n");
    txEnd();
    return false;
sdasend:
    txEnd();
    return true;
}
bool sendCancelPending(struct pendingData* pending) {
    if (!apInfo.isOnline) return false;
    if (!txStart()) return false;
    addCRC(pending, sizeof(struct pendingData));
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        cmdReplyValue = CMD_REPLY_WAIT;
        AP_SERIAL_PORT.print("CXD>");
        for (uint8_t c = 0; c < sizeof(struct pendingData); c++) {
            AP_SERIAL_PORT.write(((uint8_t*)pending)[c]);
        }
        if (waitCmdReply()) goto cxdsent;
        logf("CXD send failed in try %d\n", attempt);
    }
    log("CXD failed to send...\n");
    txEnd();
    return false;
cxdsent:
    txEnd();
    return true;
}
bool sendChannelPower(struct espSetChannelPower* scp) {
    if ((apInfo.state != AP_STATE_ONLINE) && (apInfo.state != AP_STATE_COMING_ONLINE)) return false;
    if (!txStart()) return false;
    addCRC(scp, sizeof(struct espSetChannelPower));
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        cmdReplyValue = CMD_REPLY_WAIT;
        AP_SERIAL_PORT.print("SCP>");
        for (uint8_t c = 0; c < sizeof(struct espSetChannelPower); c++) {
            AP_SERIAL_PORT.write(((uint8_t*)scp)[c]);
        }
        if (waitCmdReply()) goto scpSent;
        logf("SCP send failed in try %d\n", attempt);
    }
    log("SCP failed to send...\n");
    txEnd();
    return false;
scpSent:
    txEnd();
    return true;
}
bool sendPing() {
    if (!txStart()) return false;
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        cmdReplyValue = CMD_REPLY_WAIT;
        AP_SERIAL_PORT.print("RDY?");
        if (waitCmdReply()) goto pingSent;
    }
    txEnd();
    return false;
pingSent:
    txEnd();
    return true;
}
bool sendGetInfo() {
    if (!txStart()) return false;
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        cmdReplyValue = CMD_REPLY_WAIT;
        AP_SERIAL_PORT.print("NFO?");
        if (waitCmdReply()) goto nfoRequested;
    }
    txEnd();
    return false;
nfoRequested:
    txEnd();
    return true;
}

// add RX'd request from the AP to the processor queue
void addRXQueue(uint8_t* data, uint8_t len, uint8_t type) {
    struct rxCmd* rxcmd = new struct rxCmd;
    rxcmd->data = data;
    rxcmd->len = len;
    rxcmd->type = type;
    BaseType_t queuestatus = xQueueSend(rxCmdQueue, &rxcmd, 0);
    if (queuestatus == pdFALSE) {
        if (data) free(data);
        free(rxcmd);
    }
}

// Asynchronous command processor
void rxCmdProcessor(void* parameter) {
    rxCmdQueue = xQueueCreate(30, sizeof(struct rxCmd*));
    txActive = xSemaphoreCreateBinary();
    xSemaphoreGive(txActive);
    while (1) {
        struct rxCmd* rxcmd = nullptr;
        BaseType_t q = xQueueReceive(rxCmdQueue, &rxcmd, 10);
        if (q == pdTRUE) {
            switch (rxcmd->type) {
                case RX_CMD_RQB:
                    processBlockRequest((struct espBlockRequest*)rxcmd->data);
#ifdef HAS_RGB_LED
                    shortBlink(CRGB::Blue);
#endif
                    break;
                case RX_CMD_ADR:
                    processDataReq((struct espAvailDataReq*)rxcmd->data, true);
#ifdef HAS_RGB_LED
                    shortBlink(CRGB::Aqua);
#endif
                    break;
                case RX_CMD_XFC:
                    processXferComplete((struct espXferComplete*)rxcmd->data, true);
#ifdef HAS_RGB_LED
                    shortBlink(CRGB::Purple);
#endif
                    break;
                case RX_CMD_XTO:
                    processXferTimeout((struct espXferComplete*)rxcmd->data, true);
                    break;
            }
            if (rxcmd->data) free(rxcmd->data);
            if (rxcmd) free(rxcmd);
        }
    }
}
void rxSerialTask(void* parameter) {
    static char cmdbuffer[4] = {0};
    static uint8_t* packetp = nullptr;
    static uint8_t pktindex = 0;  // length of the command
    static uint8_t RXState = ZBS_RX_WAIT_HEADER;
    static char lastchar = 0;
    static uint8_t charindex = 0;

    while (1) {
        while (AP_SERIAL_PORT.available()) {
            lastchar = AP_SERIAL_PORT.read();
            switch (RXState) {
                case ZBS_RX_WAIT_HEADER:
                    //  shift characters in
                    for (uint8_t c = 0; c < 3; c++) {
                        cmdbuffer[c] = cmdbuffer[c + 1];
                    }
                    cmdbuffer[3] = lastchar;

                    if ((strncmp(cmdbuffer, "ACK>", 4) == 0)) cmdReplyValue = CMD_REPLY_ACK;
                    if ((strncmp(cmdbuffer, "NOK>", 4) == 0)) cmdReplyValue = CMD_REPLY_NOK;
                    if ((strncmp(cmdbuffer, "NOQ>", 4) == 0)) cmdReplyValue = CMD_REPLY_NOQ;

                    if ((strncmp(cmdbuffer, "VER>", 4) == 0)) {
                        pktindex = 0;
                        RXState = ZBS_RX_WAIT_VER;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if ((strncmp(cmdbuffer, "MAC>", 4) == 0)) {
                        RXState = ZBS_RX_WAIT_MAC;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if ((strncmp(cmdbuffer, "ZCH>", 4) == 0)) {
                        RXState = ZBS_RX_WAIT_CHANNEL;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if ((strncmp(cmdbuffer, "ZPW>", 4) == 0)) {
                        RXState = ZBS_RX_WAIT_POWER;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if ((strncmp(cmdbuffer, "PEN>", 4) == 0)) {
                        RXState = ZBS_RX_WAIT_PENDING;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if ((strncmp(cmdbuffer, "NOP>", 4) == 0)) {
                        RXState = ZBS_RX_WAIT_NOP;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if ((strncmp(cmdbuffer, "TYP>", 4) == 0)) {
                        RXState = ZBS_RX_WAIT_TYPE;
                        charindex = 0;
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if (strncmp(cmdbuffer, "RES>", 4) == 0) {
                        addRXQueue(NULL, 0, RX_CMD_RSET);
                    }
                    if (strncmp(cmdbuffer, "RQB>", 4) == 0) {
                        RXState = ZBS_RX_BLOCK_REQUEST;
                        charindex = 0;
                        pktindex = 0;
                        packetp = (uint8_t*)calloc(sizeof(struct espBlockRequest) + 8, 1);
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if (strncmp(cmdbuffer, "ADR>", 4) == 0) {
                        RXState = ZBS_RX_WAIT_DATA_REQ;
                        charindex = 0;
                        pktindex = 0;
                        packetp = (uint8_t*)calloc(sizeof(struct espAvailDataReq) + 8, 1);
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if (strncmp(cmdbuffer, "XFC>", 4) == 0) {
                        RXState = ZBS_RX_WAIT_XFERCOMPLETE;
                        pktindex = 0;
                        packetp = (uint8_t*)calloc(sizeof(struct espXferComplete) + 8, 1);
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if (strncmp(cmdbuffer, "XTO>", 4) == 0) {
                        RXState = ZBS_RX_WAIT_XFERTIMEOUT;
                        pktindex = 0;
                        packetp = (uint8_t*)calloc(sizeof(struct espXferComplete) + 8, 1);
                        memset(cmdbuffer, 0x00, 4);
                    }
                    if (strncmp(cmdbuffer, "RDY>", 4) == 0) {
                        addRXQueue(NULL, 0, RX_CMD_RDY);
                    }
                    break;
                case ZBS_RX_BLOCK_REQUEST:
                    packetp[pktindex] = lastchar;
                    pktindex++;
                    if (pktindex == sizeof(struct espBlockRequest)) {
                        addRXQueue(packetp, pktindex, RX_CMD_RQB);
                        RXState = ZBS_RX_WAIT_HEADER;
                    }
                    break;
                case ZBS_RX_WAIT_XFERCOMPLETE:
                    packetp[pktindex] = lastchar;
                    pktindex++;
                    if (pktindex == sizeof(struct espXferComplete)) {
                        addRXQueue(packetp, pktindex, RX_CMD_XFC);
                        RXState = ZBS_RX_WAIT_HEADER;
                    }
                    break;
                case ZBS_RX_WAIT_XFERTIMEOUT:
                    packetp[pktindex] = lastchar;
                    pktindex++;
                    if (pktindex == sizeof(struct espXferComplete)) {
                        addRXQueue(packetp, pktindex, RX_CMD_XTO);
                        RXState = ZBS_RX_WAIT_HEADER;
                    }
                    break;
                case ZBS_RX_WAIT_DATA_REQ:
                    packetp[pktindex] = lastchar;
                    pktindex++;
                    if (pktindex == sizeof(struct espAvailDataReq)) {
                        addRXQueue(packetp, pktindex, RX_CMD_ADR);
                        RXState = ZBS_RX_WAIT_HEADER;
                    }
                    break;
                case ZBS_RX_WAIT_VER:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 4) {
                        charindex = 0;
                        apInfo.version = (uint16_t)strtoul(cmdbuffer, NULL, 16);
                        RXState = ZBS_RX_WAIT_HEADER;
                    }
                    break;
                case ZBS_RX_WAIT_MAC:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 2) {
                        charindex = 0;
                        apInfo.mac[pktindex] = (uint8_t)strtoul(cmdbuffer, NULL, 16);
                        pktindex++;
                    }
                    if (pktindex == 8) {
                        RXState = ZBS_RX_WAIT_HEADER;
                    }
                    break;
                case ZBS_RX_WAIT_CHANNEL:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 2) {
                        RXState = ZBS_RX_WAIT_HEADER;
                        apInfo.channel = (uint8_t)strtoul(cmdbuffer, NULL, 16);
                    }
                    break;
                case ZBS_RX_WAIT_POWER:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 2) {
                        RXState = ZBS_RX_WAIT_HEADER;
                        apInfo.power = (uint8_t)strtoul(cmdbuffer, NULL, 16);
                    }
                    break;
                case ZBS_RX_WAIT_PENDING:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 2) {
                        RXState = ZBS_RX_WAIT_HEADER;
                        apInfo.pending = (uint8_t)strtoul(cmdbuffer, NULL, 16);
                    }
                    break;
                case ZBS_RX_WAIT_NOP:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 2) {
                        RXState = ZBS_RX_WAIT_HEADER;
                        apInfo.nop = (uint8_t)strtoul(cmdbuffer, NULL, 16);
                    }
                    break;
                case ZBS_RX_WAIT_TYPE:
                    cmdbuffer[charindex] = lastchar;
                    charindex++;
                    if (charindex == 2) {
                        RXState = ZBS_RX_WAIT_HEADER;
                        apInfo.type = (uint8_t)strtoul(cmdbuffer, NULL, 16);
                    }
                    break;
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }  // end of while(1)
}

bool bringAPOnline() {
    apInfo.isOnline = false;
    apInfo.state = AP_STATE_OFFLINE;
    uint32_t bootTimeout = millis();
    bool APrdy = false;
    while ((!APrdy) && (millis() - bootTimeout < 5 * 1000)) {
        APrdy = sendPing();
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    if (!APrdy) {
        send_ap_info();
        return false;
    } else {
        apInfo.state = AP_STATE_COMING_ONLINE;
        sendChannelPower(&curChannel);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        if (!sendGetInfo()) {
            apInfo.state = AP_STATE_OFFLINE;
            send_ap_info();
            return false;
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
        apInfo.isOnline = true;
        apInfo.state = AP_STATE_ONLINE;
        send_ap_info();
        return true;
    }
}

void APTask(void* parameter) {
    xTaskCreate(rxCmdProcessor, "rxCmdProcessor", 3000, NULL, configMAX_PRIORITIES - 10, NULL);
    xTaskCreate(rxSerialTask, "rxSerialTask", 1750, NULL, configMAX_PRIORITIES - 4, NULL);

#if (AP_PROCESS_PORT == FLASHER_AP_PORT)
    AP_SERIAL_PORT.begin(115200, SERIAL_8N1, FLASHER_AP_RXD, FLASHER_AP_TXD);
#endif
#ifdef OPENEPAPERLINK_PCB
#if (AP_PROCESS_PORT == FLASHER_EXT_PORT)
    AP_SERIAL_PORT.begin(115200, SERIAL_8N1, FLASHER_EXT_RXD, FLASHER_EXT_TXD);
#endif
#if (AP_PROCESS_PORT == FLASHER_ALTRADIO_PORT)
    AP_SERIAL_PORT.begin(115200, SERIAL_8N1, FLASHER_AP_RXD, FLASHER_AP_TXD);
#endif
#endif

    bringAPOnline();

    uint8_t attempts = 0;
    while (1) {
        if ((apInfo.isOnline) && (millis() - lastAPActivity > AP_ACTIVITY_MAX_INTERVAL)) {
            bool reply = sendPing();
            if (!reply) {
                attempts++;
            } else {
                attempts = 0;
                sendGetInfo();
                send_ap_info();
            }
            if (attempts > 5) {
                apInfo.state = AP_STATE_WAIT_RESET;
                apInfo.isOnline = false;
                if (!bringAPOnline()) {
                    // tried to reset the AP, but we failed... Maybe the AP-Tag died?
                    apInfo.state = AP_STATE_FAILED;
#ifdef HAS_RGB_LED
                    showColorPattern(CRGB::Yellow, CRGB::Yellow, CRGB::Red);
#endif
                } else {
                    apInfo.state = AP_STATE_ONLINE;
                    apInfo.isOnline = true;
                    attempts = 0;
                }
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}