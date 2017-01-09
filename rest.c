/*
 * rest.c
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
#include <json-c/json.h>
#include <curl/curl.h>
#include "rest.h"

#define TIMEOUT     10000L

struct {
	// Threading
	pthread_t sender_thread;
	pthread_mutex_t thread_lock;

	// Internal message list
	struct queue_message *first_message;
	struct queue_message *last_message;

	char* uri;

	CURL *curl;
} Rest;

struct queue_message {
	json_object *message;
	struct queue_message *next;
};

struct queue_message *popFirstMessageInQueue();
void sendMessagesToRestInterface();
void sendMessage(json_object *json);

/* Initialize things */
int initRestConnection(char* uri) {

	printf("Rest connection settings: %s\n", uri);
	Rest.uri = uri;
	Rest.first_message = NULL;

	pthread_mutex_init(&Rest.thread_lock,NULL);
	pthread_create(&Rest.sender_thread, NULL, sendMessagesToRestInterface, NULL);

	Rest.curl = curl_easy_init();
	if(Rest.curl) {
	    struct curl_slist *headers = NULL;
	    headers = curl_slist_append(headers, "Accept: application/json");
	    headers = curl_slist_append(headers, "Content-Type: application/json");
	    curl_easy_setopt(Rest.curl, CURLOPT_CUSTOMREQUEST, "POST");
	    curl_easy_setopt(Rest.curl, CURLOPT_HTTPHEADER, headers);
	    curl_easy_setopt(Rest.curl, CURLOPT_URL, uri);
//	    curl_easy_setopt(Rest.curl, CURLOPT_VERBOSE, 1L);
	}

	return 0;
}

/* Queue functionality */
void addRawMessageToRestQueue(char *data, int length) {
	if(!Rest.uri) {
		return;
	}
	struct queue_message *curr = malloc(sizeof(struct queue_message));
	struct timeval tv;

	gettimeofday(&tv, NULL);
	unsigned long long millisecondsSinceEpoch =
	    (unsigned long long)(tv.tv_sec) * 1000 +
	    (unsigned long long)(tv.tv_usec) / 1000;

	data[length-1] = '\0';
	json_object *json = json_object_new_object();
	json_object *adsb_data = json_object_new_string(data);
	json_object *timestamp = json_object_new_int64(millisecondsSinceEpoch);

	json_object_object_add(json, "message",  adsb_data);
	json_object_object_add(json, "timestamp", timestamp);

	curr->message = json;

	addMessageToQueue(curr);
}

void addMessageToQueue(struct queue_message *msg) {
	pthread_mutex_lock(&Rest.thread_lock);
        struct queue_message *tail = Rest.last_message;
        if(tail) {
                tail->next = msg;
        } else {
                Rest.first_message = msg;
        }
        Rest.last_message = msg;
        pthread_mutex_unlock(&Rest.thread_lock);
}


struct queue_message *popFirstMessageInQueue() {
	pthread_mutex_lock(&Rest.thread_lock);
	struct queue_message *msg = Rest.first_message;
	if(Rest.first_message == Rest.last_message) {
		Rest.first_message = 0;
		Rest.last_message = 0;
	} else {
		Rest.first_message = Rest.first_message->next;
	}
	pthread_mutex_unlock(&Rest.thread_lock);
	return msg;
}

/* REST */
void sendMessagesToRestInterface() {
	json_object *json = json_object_new_object();
	int message_counter = 1;
	int wait_counter = 0;
	while(1) {
		// Send batch in case we either have 100 messages in queue or we have waited 5s and have messages to send
		if(message_counter > 100 || (message_counter > 1 && wait_counter > 100)) {
			sendMessage(json);
			message_counter = 1;
			wait_counter = 0;
			json_object_put(json);
			json = json_object_new_object();
		}
		else if (Rest.first_message) {
			struct queue_message *msg = popFirstMessageInQueue();
			if(msg) {
				char buf[9];
				sprintf(buf, "entry-%d", message_counter);
				json_object_object_add(json, buf, msg->message);
				message_counter++;
				free(msg);
				continue;
			}
		} else {
			// Sleep for 50ms
			usleep(50000);
			wait_counter++;
		}
	}
}

void sendMessage(json_object *json) {
	curl_easy_setopt(Rest.curl, CURLOPT_POSTFIELDS, json_object_to_json_string(json));
	curl_easy_perform(Rest.curl);
//	printf("\nSending message %s\n", json_object_to_json_string(json));
}
