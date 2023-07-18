#ifndef _RADIO_H_
#define _RADIO_H_

#include <stdbool.h>
#include <stdint.h>


void RF_IRQ1(void) __interrupt (4);
void RF_IRQ2(void) __interrupt (5);
#define RADIO_PAD_LEN_BY		2
	
extern uint8_t __code channelList[6];

#include "../radioCommon.h"

#endif