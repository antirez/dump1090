/*
 * Mqtt.h
 *
 *  Created on: Apr 18, 2015
 *      Author: borax
 */

#ifndef MQTT_H_
#define MQTT_H_

int initMqConnection(char* uri, char* username, char* password);
void shutdownMqConnection();
void addRawMessageToMq(char *data, int length);


#endif /* MQTT_H_ */
