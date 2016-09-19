/*
 * power.h
 *
 *  Created on: Sep 19, 2016
 *      Author: ChauNM
 */

#ifndef POWER_H_
#define POWER_H_

#define POWER_STATE_PIN		23

#define POWER_ON			1
#define POWER_OFF			0

#define BATTERY_DURATION 	7200

typedef struct tagPOWERACTOROPTION {
	char* guid;
	char* psw;
	char* host;
	WORD port;
}POWERACTOROPTION, *PPOWERACTOROPTION;


#endif /* POWER_H_ */
