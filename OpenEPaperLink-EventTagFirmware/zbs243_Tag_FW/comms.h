#ifndef _COMMS_H_
#define _COMMS_H_

#include <stdint.h>


#define COMMS_MAX_RADIO_WAIT_MSEC		200

#define COMMS_RX_ERR_NO_PACKETS			(-1)
#define COMMS_RX_ERR_INVALID_PACKET		(-2)

#define COMMS_MAX_PACKET_SZ				(127)

extern uint8_t __xdata mLastLqi;
extern int8_t __xdata mLastRSSI;

int8_t commsRxUnencrypted(void __xdata *data);
bool commsTxUnencrypted(const void __xdata *packetP, uint8_t len);

bool commsTxNoCpy(const void __xdata *packetp);


#endif
