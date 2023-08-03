import time

import serial
import ctypes
import hashlib
import typing
import logging
from time import sleep
from c_util import hex_reverse_bytes, hex_bytes
from proto_def import AvailableDataRequest, AvailDataInfo, BlockRequest, BlockHeader, XferComplete
from mqtt import MqttHandler


BLOCK_SIZE = 4096
T = typing.TypeVar('T')


class Logic:
    def __init__(self):
        self.log = logging.getLogger("logic")
        self.log.setLevel(logging.DEBUG)
        self.mqtt = MqttHandler()

    def main_loop(self):
        while True:
            sleep(1)

    def handle_adr(self, adr: AvailableDataRequest):
        data_type = 0;
        """
        handles an AvailableDataRequest, which is issued by the AP
        whenever a tag checks-in with the AP. The tag provides some
        useful data - which we store.
        we then check, if there is new data available for the tag -
        hence the method name. if so, we generate an AvailDataInfo,
        which the AP will deliver to the tag on its next check-in
        """
        self.log.debug(f"{adr}")
        pretty_mac = hex_reverse_bytes(adr.sourceMac)
        self.log.info(f"Checkin from MAC {pretty_mac}")
        # lookup the tag in the DB, might create it, if it's new
        tag = self.db.get_tag(pretty_mac)
        tag['lastCheckin'] = int(time.time())
        if adr.lastPacketLQI != 0:
            tag['lastPacketLQI'] = adr.lastPacketLQI
        if adr.lastPacketRSSI != 0:
            tag['lastPacketRSSI'] = adr.lastPacketRSSI
        if adr.temperature != 0:
            tag['temperature'] = adr.temperature
        if adr.batteryMv != 0:
            tag['batteryMv'] = adr.batteryMv
        tag['hwType'] = adr.hwType
        
        mac = hex_reverse_bytes(adr.sourceMac, None)
        if self.pic.has_image(mac + ".bin"):
            # type 0x03 firmware update
            data_type = 0x03
            data = self.pic.get_image(mac + ".bin", data_type)
        elif self.pic.has_image(mac + ".png"):
            # we just assume, the picture and tag are bi-color (black and red)
            # type 0x20 would be "just black"
            data_type = 0x21
            data = self.pic.get_image(mac + ".png", data_type)
        else:
            self.log.info(f"No image found for {pretty_mac}")
            return
        
        # the tag and AP firmware uses a 64-bit "version" to check, whether
        # it has an up-to date image. we currently just use half of a MD5-hash
        # of a picture to generate a new value, if the picture data changes.
        md5 = hashlib.md5(data).digest()
        data_version = hex_bytes(md5[:8])

        # out tag-db knows (caches) which imageVersion was last acknowledged (XFC-command)
        # by the tag / AP. if the tags current image version is identical to our local image version,
        # the tag is up-to-date
        if 'imageVersion' not in tag or tag['imageVersion'] != data_version:
            tag['pendingVersion'] = data_version

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
                targetMac=adr.sourceMac,
            )
            # OpenEPaper checksums seem to be quite literal "checksums" even though they call them CRCs
            # in their firmware. they're just 8- or 16-bit integers, which overflow while adding.
            # we recreate this native overflow behaviour by MOD-0x100-ing them
            sda.checksum = sum(bytes(sda)[1:]) % 0x100
            self.log.info(f"Tag needs new data. sending {sda}")
            self.serial_write(b'SDA>')
            self.serial_write(bytes(sda))
        else:
            self.log.info("Tag image is up to date")
        self.db.notify_change(pretty_mac)
        # persist our database changes
        # NOTE: this is stupid, simple and lazy database implementation,
        #       but sufficient for now. we can change to something fancy later.
        self.db.save()

    def handle_rqb(self, rqb: BlockRequest):
        """
        Handles a BlockRequest, which is issued by the AP when it needs data
        to transfer to a tag. data is split-up into blocks of 4096 bytes, prefixed
        with a header and sent with a special encoding (XORd with 0xAA) - only god
        knows why, as this only makes sense, if the receiver can recover timing info
        from the signal (maybe?)
        """
        data_type = 0
        filename = ""
        self.log.debug(f"{rqb}")
        self.log.info(f"Got RQB for MAC {hex_reverse_bytes(rqb.srcMac)}")
        mac = hex_reverse_bytes(rqb.srcMac, None)
        if self.pic.has_image(mac + ".bin"):
             data_type = 0x03
             filename = mac + ".bin"
             cancel = False
        elif self.pic.has_image(mac + ".png"):
             data_type = 0x21
             filename = mac + ".png"
             cancel = False
        else:
            self.log.warning(f"No image found for {hex_reverse_bytes(rqb.srcMac)}")
            cancel = True
           
        if cancel:
            # cancel block request
            self.log.info("Sending: cancel block request")
            cxd = AvailDataInfo(targetMac=rqb.srcMac)
            cxd.checksum = sum(bytes(cxd)[1:]) & 0x100
            self.serial_write(b'CXD>')
            self.serial_write(bytes(cxd))
        else:
            # we just assume, that the data is black-and-red (0x21) - see handle_adr()
            data = self.pic.get_image(filename, data_type)
            offset = rqb.blockId * BLOCK_SIZE
            length = min(len(data) - offset, BLOCK_SIZE)
            self.log.info(f"Transmitting block {rqb.blockId} of length {length}")
            transmit_data = data[offset:offset + length]
            self.log.debug(f"Block bytes: {transmit_data.hex()}")
            header = bytes(BlockHeader(
                length=length,
                # see handle_adr() for checksum-foo
                checksum=sum(transmit_data) % 0x10000
            ))
            self.serial_write(b'>D>')
            # waiting for a little bit, but not too long, seems to be required for successful transmission.
            # this may be due to the fact, that serial-command processing and the below bulk-transfer are
            # separate processes, as the below bulk-transfer is implemented as a kind of interrupt-driven DMA
            # in the AP.
            sleep(.05)
            # the AP-firmware XORs the data back on retrieval
            self.serial_write(bytes(b ^ 0xAA for b in header))
            self.serial_write(bytes(b ^ 0xAA for b in transmit_data))
            # the AP-firmware expects bulk-transfers to always be 4100 bytes (due to the DMA mechanism)
            # thus we need to fill the block with junk (0xFF when unXORd in this case)
            self.serial_write(bytes([0x55] * (BLOCK_SIZE - length)))
            # the ESP-firmware also sends some additional junk "in case some bytes were missed" !?
            self.serial_write(bytes([0xF5] * 32), False)

    def handle_xfc(self, val: XferComplete):
        """
        handles a XFC (transfer-complete) message, which is issued by the AP,
        when it "knows" that a tag has fully received the data, announced by a previous
        AvailDataInfo. this may happen without any actual data-transfer - e.g. if the
        tag has fetched the data from EPROM
        """
        pretty_mac = hex_reverse_bytes(val.srcMac)
        mac = hex_reverse_bytes(val.srcMac, None)
        self.log.info(f"Got XFC for Mac {pretty_mac}")
        tag = self.db.get_tag(pretty_mac)
        if 'pendingVersion' not in tag:
            # should not happen but also is uncritical
            self.log.warning("WARNING: pendingVersion not found on XFC")
        else:
            # persist fact, that the tags imageVersion now is identical to its pendingVersion
            tag['imageVersion'] = tag['pendingVersion']
            if self.pic.has_image(mac + ".bin"):
                self.pic.remove_image(mac + ".bin")
            self.db.notify_change(pretty_mac)
            self.db.save()


if __name__ == '__main__':
    logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    Logic().main_loop()
