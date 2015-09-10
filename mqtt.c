/*
 * Mqtt.c
 *
 *  Created on: Apr 18, 2015
 *      Author: borax
 */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "mqtt.h"
#include "MQTTClient.h"

#define CLIENTID    "Dump1090"
#define TOPIC       "adsb/data/raw"
#define QOS         0
#define TIMEOUT     10000L

struct {
	// Threading
	pthread_t sender_thread;
	pthread_mutex_t thread_lock;
	pthread_mutex_t thread_lock2;
	pthread_cond_t thread_signal;
	pthread_cond_t thread_new_message;

	int testa;
	int testb;

	// Internal message list
	struct queue_message *first_message;
	struct queue_message *last_message;

	// MQTT connection options
	char* uri;
	char* username;
	char* password;
	MQTTClient client;
	MQTTClient_deliveryToken token;
} Mqtt;

struct queue_message {
	char *message;
	int length;
	struct queue_message *next;
};

struct queue_message *popFirstMessageInQueue();
void sendMessagesToMq();
void sendMessageToMq(struct queue_message *msg);

/* Initialize things */
int initMqConnection(char* uri, char* username, char* password) {

	printf("MQ connection settings: %s %s %s\n", uri, username, password);
	Mqtt.uri = uri;
	Mqtt.username = username;
	Mqtt.password = password;
	Mqtt.first_message = NULL;

	pthread_mutex_init(&Mqtt.thread_lock,NULL);
	pthread_mutex_init(&Mqtt.thread_lock2,NULL);
	pthread_cond_init(&Mqtt.thread_signal,NULL);
	pthread_cond_init(&Mqtt.thread_new_message,NULL);
	pthread_create(&Mqtt.sender_thread, NULL, sendMessagesToMq, NULL);

	Mqtt.testa = 0;
	Mqtt.testb = 0;

	return 0;
}

void shutdownMqConnection() {
	printf("Closing MQ connection\n");

}

/* Queue functionality */
void addRawMessageToMq(char *data, int length) {
	if(!Mqtt.uri) {
		return;
	}
	struct queue_message *curr = malloc(sizeof(struct queue_message));
	struct timeval tv;

	gettimeofday(&tv, NULL);
	unsigned long long millisecondsSinceEpoch =
	    (unsigned long long)(tv.tv_sec) * 1000 +
	    (unsigned long long)(tv.tv_usec) / 1000;

        data[length-1] = '\0';

        printf("{ \"timeSinceEpochUTC\":%llu, \"message\":\"%s\" }\n", millisecondsSinceEpoch, data);

	curr->message = malloc(length+1);
	memcpy(curr->message, data, length);
	curr->message[length] = '\0';
	curr->length = length;

	addMessageToQueue(curr);
}

void addMessageToQueue(struct queue_message *msg) {
	pthread_mutex_lock(&Mqtt.thread_lock);
//	pthread_cond_wait(&Mqtt.thread_signal,&Mqtt.thread_lock);
        struct queue_message *tail = Mqtt.last_message;
        if(tail) {
                tail->next = msg;
        } else {
                Mqtt.first_message = msg;
        }
        Mqtt.last_message = msg;
        pthread_mutex_unlock(&Mqtt.thread_lock);
//        pthread_cond_broadcast(&Mqtt.thread_new_message);
}


struct queue_message *popFirstMessageInQueue() {
	pthread_mutex_lock(&Mqtt.thread_lock);
/*	while(Mqtt.first_message == 0) {
		pthread_cond_wait(&Mqtt.thread_new_message,&Mqtt.thread_lock);
	}*/
	struct queue_message *msg = Mqtt.first_message;
//	Mqtt.first_message = Mqtt.first_message->next;
	if(Mqtt.first_message == Mqtt.last_message) {
		Mqtt.first_message = 0;
		Mqtt.last_message = 0;
	} else {
		Mqtt.first_message = Mqtt.first_message->next;
	}
	pthread_mutex_unlock(&Mqtt.thread_lock);
//	pthread_cond_broadcast(&Mqtt.thread_signal);
	return msg;
}

/* Mqtt */
void sendMessagesToMq() {
	while(1) {
		if (Mqtt.first_message != NULL) {
			struct queue_message *msg = popFirstMessageInQueue();
			if(msg != NULL) {
				sendMessageToMq(msg);
				free(msg);
			}
//			usleep(50);
		}
		usleep(50);
/*		else {
			printf("Waiting for data\n");
			sleep(1);
		}
*/
	}
}

void initiateConnection() {
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	int rc;

	MQTTClient_create(&Mqtt.client, Mqtt.uri, CLIENTID,
			MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;

	if(Mqtt.username) {
		conn_opts.username = Mqtt.username;
		conn_opts.password = Mqtt.password;
	}

	if ((rc = MQTTClient_connect(Mqtt.client, &conn_opts)) != MQTTCLIENT_SUCCESS)
	{
		printf("Failed to connect, return code %d\n", rc);
	}
}

void destroyConnection() {
	MQTTClient_disconnect(Mqtt.client, 10000);
	MQTTClient_destroy(&Mqtt.client);
}

void sendMessageToMq(struct queue_message *msg) {
	if(!Mqtt.client) {
		initiateConnection();
	}

        printf("Sending: %s \n", msg->message);

	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	pubmsg.payload = msg->message;
	pubmsg.payloadlen = msg->length;
	pubmsg.qos = QOS;
	pubmsg.retained = 0;

	if(MQTTClient_publishMessage(Mqtt.client, TOPIC, &pubmsg, &Mqtt.token) != MQTTCLIENT_SUCCESS) {
		destroyConnection();
		return;
	}
	if(MQTTClient_waitForCompletion(Mqtt.client, Mqtt.token, TIMEOUT) != MQTTCLIENT_SUCCESS) {
		destroyConnection();
		return;
	}
}
