import ctypes
from time import time

import paho.mqtt.client as mqtt
import logging
import json
import hashlib
from db import TagDb
from pic_fs import PicFS
from proto_def import AvailDataInfo
from web import Web
from c_util import hex_bytes

class MqttHandler:

    def __init__(self):
        self.log = logging.getLogger("mqtt")
        self.log.setLevel(logging.DEBUG)
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.connect("127.0.0.1", 1883, 60)
        self.client.loop_start()

        self.db = TagDb()
        # load from ./cache/tagdb.json
        # does nothing, if it doesn't exist
        self.db.load()
        # utility for accessing and converting pictures
        self.pic = PicFS()
        self.websockets = Web(self.db)


    # The callback for when the client receives a CONNACK response from the server.
    def on_connect(self, client, userdata, flags, rc):
        self.log.info(f"Connected with result code {rc}")
        # Subscribing in on_connect() means that if we lose the connection and
        # reconnect then subscriptions will be renewed.
        client.subscribe("#")

    # The callback for when a PUBLISH message is received from the server.
    def on_message(self, client, userdata, msg):
        if msg.topic == '/tag/sda':
            # our own (and much) traffic -> don't log
            return
        self.log.info(f"{msg.topic} {msg.payload}")
        if msg.topic == '/tag/adr':
            self.handle_adr(json.loads(msg.payload))
        if msg.topic == '/tag/xfc':
            self.handle_xfc(json.loads(msg.payload))

    def handle_adr(self, adr):
        mac: str = adr['src']
        tag = self.db.get_tag(mac)
        for k in ['src', 'last_lqi', 'last_rssi', 'temperature', 'battery_mv', 'mqtt_id']:
            tag[k] = adr[k]
        tag['last_checkin'] = time()

        if self.pic.has_image(mac.replace(':', '') + ".png"):
            # we just assume, the picture and tag are bi-color (black and red)
            # type 0x20 would be "just black"
            data_type = 0x21
            data = self.pic.get_image(mac.replace(':', '') + ".png", data_type)
        else:
            self.log.info(f"No image found for {mac}")
            return

        # the tag and AP firmware uses a 64-bit "version" to check, whether
        # it has an up-to date image. we currently just use half of a MD5-hash
        # of a picture to generate a new value, if the picture data changes.
        md5 = hashlib.md5(data).digest()
        data_version = hex_bytes(md5[:8])

        if 'imageVersion' not in tag or tag['imageVersion'] != data_version:
            tag['pendingVersion'] = data_version
            self.log.info(f"Tag {mac} needs new data. sending...")
            sda = AvailDataInfo(
                checksum=0,
                dataVer=(ctypes.c_ubyte * 8).from_buffer_copy(md5[:8]),
                dataSize=len(data),
                dataType=data_type,
                # the tag firmware know multiple "LUT"s (whatever that means).
                # it seems to be related to the updating of the epaper contents.
                # EPD_LUT_DEFAULT 0
                # EPD_LUT_NO_REPEATS 1
                # EPD_LUT_FAST_NO_REDS 2
                # EPD_LUT_FAST 3
                dataTypeArgument=1,
                nextCheckIn=0,  # default taken from ESP firmware
                attemptsLeft=60 * 24,  # default taken from ESP firmware
                targetMac=(ctypes.c_ubyte * 8).from_buffer_copy(bytes.fromhex(mac.replace(':', ''))[::-1])
            )
            self.client.publish("/tag/sda", bytes(sda) + data)
        self.db.notify_change(mac)
        # persist our database changes
        # NOTE: this is stupid, simple and lazy database implementation,
        #       but sufficient for now. we can change to something fancy later.
        self.db.save()

    def handle_xfc(self, xfc):
        mac: str = xfc['src']
        tag = self.db.get_tag(mac)
        tag['imageVersion'] = tag['pendingVersion']
        self.db.notify_change(mac)
        self.db.save()
