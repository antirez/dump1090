/*
 * Mqtt.h
 *
 *  Created on: Apr 18, 2015
 *      Author: borax
 */

#ifndef REST_H_
#define REST_H_

int initRestConnection(char* uri);
void addRawMessageToRestQueue(char *data, int length);


#endif /* REST_H_ */
