/*
 * power.c
 *
 *  Created on: Sep 19, 2016
 *      Author: ChauNM
 */

#include "power.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include "jansson.h"
#include "lib/wiringPi/wiringPi.h"
#include "universal.h"
#include "Actor/actor.h"
#include "strings.h"

#define MAIN_LOOP_PERIOD	10000

static PACTOR PowerActor = NULL;
static BOOL powerState;
static unsigned long backoutDuration = 0;
static unsigned long blackoutTimeStamp;

static void PowerSetupGpio()
{
	char command[255];
	printf("Init IO for power service\n");
	sprintf(command, "gpio export %d in", POWER_STATE_PIN);
	system(command);
	wiringPiSetupSys();
	pinMode(POWER_STATE_PIN, INPUT);
	if (digitalRead(POWER_STATE_PIN) == HIGH)
		powerState = POWER_ON;
	else
		powerState = POWER_OFF;
}

static void PowerActorOnHiRequest(PVOID pParam)
{
	char* message = (char*) pParam;
	char **znpSplitMessage;
	if (PowerActor == NULL) return;
	json_t* responseJson = NULL;
	json_t* statusJson = NULL;
	PACTORHEADER header;
	char* responseTopic;
	char* responseMessage;
	znpSplitMessage = ActorSplitMessage(message);
	if (znpSplitMessage == NULL)
		return;
	// parse header to get origin of message
	header = ActorParseHeader(znpSplitMessage[0]);
	if (header == NULL)
	{
		ActorFreeSplitMessage(znpSplitMessage);
		return;
	}
	//make response package
	responseJson = json_object();
	statusJson = json_object();
	json_t* requestJson = json_loads(znpSplitMessage[1], JSON_DECODE_ANY, NULL);
	json_object_set(responseJson, "request", requestJson);
	json_decref(requestJson);
	json_t* resultJson = json_string("status.success");
	json_object_set(statusJson, "status", resultJson);
	json_decref(resultJson);
	json_t* blackoutJson = NULL;
	json_t* blackoutTimeStampJson = NULL;
	json_t* durationTimeJson = NULL;
	json_t* elapsedTimeJson;
	if (powerState == POWER_OFF)
	{
		blackoutJson = json_string("true");
		blackoutTimeStampJson = json_integer(blackoutTimeStamp);
		durationTimeJson = json_integer(BATTERY_DURATION);
		elapsedTimeJson = json_integer(backoutDuration);
		json_object_set(statusJson, "blackout", blackoutJson);
		json_object_set(statusJson, "backoutDuration", blackoutTimeStampJson);
		json_object_set(statusJson, "durationTime", durationTimeJson);
		json_object_set(statusJson, "elapsedTime", elapsedTimeJson);
		json_decref(blackoutJson);
		json_decref(blackoutTimeStampJson);
		json_decref(durationTimeJson);
		json_decref(elapsedTimeJson);
	}
	else
	{
		blackoutJson = json_string("false");
		json_object_set(statusJson, "blackout", blackoutJson);
		json_decref(blackoutJson);
	}
	json_object_set(responseJson, "response", statusJson);
	json_decref(statusJson);
	responseMessage = json_dumps(responseJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	//responseTopic = ActorMakeTopicName(header->origin, "/:response");
	responseTopic = StrDup(header->origin);
	ActorFreeHeaderStruct(header);
	json_decref(responseJson);
	ActorFreeSplitMessage(znpSplitMessage);
	ActorSend(PowerActor, responseTopic, responseMessage, NULL, FALSE, "response");
	free(responseMessage);
	free(responseTopic);
}

static void PowerPublishBlackoutEvent()
{
	unsigned long long timeStamp = time(NULL);
	blackoutTimeStamp = timeStamp;
	if (PowerActor == NULL) return;
	json_t* eventJson = json_object();
	json_t* paramsJson = json_object();
	json_t* blackoutJson = json_string("true");
	json_t* blackoutTimeJson = json_integer(timeStamp);
	json_object_set(paramsJson, "blackout", blackoutJson);
	json_object_set(paramsJson, "time stamp", blackoutTimeJson);
	json_object_set(eventJson, "params", paramsJson);
	char* eventMessage = json_dumps(eventJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	//char* topicName = ActorMakeTopicName(PowerActor->guid, "/:event/info");
	char* topicName = ActorMakeTopicName("event/", PowerActor->guid, "/info");
	ActorSend(PowerActor, topicName, eventMessage, NULL, FALSE, topicName);
	json_decref(blackoutJson);
	json_decref(blackoutTimeJson);
	json_decref(paramsJson);
	json_decref(eventJson);
	free(topicName);
	free(eventMessage);
}


static void PowerPublishRestoredEvent()
{
	if (PowerActor == NULL) return;
	json_t* eventJson = json_object();
	json_t* paramsJson = json_object();
	json_t* blackoutJson = json_string("false");
	json_object_set(paramsJson, "blackout", blackoutJson);
	json_object_set(eventJson, "params", paramsJson);
	char* eventMessage = json_dumps(eventJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	char* topicName = ActorMakeTopicName("event/", PowerActor->guid, "/info");
	ActorSend(PowerActor, topicName, eventMessage, NULL, FALSE, topicName);
	json_decref(blackoutJson);
	json_decref(paramsJson);
	json_decref(eventJson);
	free(topicName);
	free(eventMessage);
}

static void PowerActorCreate(char* guid, char* psw, char* host, WORD port)
{
	PowerActor = ActorCreate(guid, psw, host, port);
	//Register callback to handle request package
	if (PowerActor == NULL)
	{
		printf("Couldn't create actor\n");
		return;
	}
	char* topicName = ActorMakeTopicName("action/", guid, "/Hi");
	ActorRegisterCallback(PowerActor, topicName, PowerActorOnHiRequest, CALLBACK_RETAIN);
	free(topicName);
}

static void PowerProcess()
{
	static DWORD blackoutCount;
	if (digitalRead(POWER_STATE_PIN) == LOW)
	{
		if (powerState == POWER_ON)
		{
			powerState = POWER_OFF;
			blackoutCount = 0;
			backoutDuration = 0;
			PowerPublishBlackoutEvent();
		}
	}
	else
	{
		if (powerState == POWER_OFF)
		{
			powerState = POWER_ON;
			PowerPublishRestoredEvent();
		}
	}
	if (powerState == POWER_OFF)
	{
		blackoutCount++;
		if (blackoutCount == (1000000 / MAIN_LOOP_PERIOD))
		{
			blackoutCount = 0;
			backoutDuration++;
		}
	}
}

void PowerActorStart(PPOWERACTOROPTION option)
{
	PowerSetupGpio();
	mosquitto_lib_init();
	printf("Create actor\n");
	PowerActorCreate(option->guid, option->psw, option->host, option->port);
	if (PowerActor == NULL)
	{
		mosquitto_lib_cleanup();
		return;
	}
	while(1)
	{
		ActorProcessEvent(PowerActor);
		PowerProcess();
		mosquitto_loop(PowerActor->client, 0, 1);
		usleep(10000);
	}
	mosquitto_disconnect(PowerActor->client);
	mosquitto_destroy(PowerActor->client);
	mosquitto_lib_cleanup();
}

int main(int argc, char* argv[])
{
	POWERACTOROPTION option;
	/* get option */
	int opt= 0;
	char *token = NULL;
	char *guid = NULL;
	char *host = NULL;
	WORD port = 0;
	printf("start power service \n");
	// specific the expected option
	static struct option long_options[] = {
			{"id",      required_argument,  0, 'i' },
			{"token", 	required_argument,  0, 't' },
			{"host", 	required_argument,  0, 'H' },
			{"port", 	required_argument, 	0, 'p' },
			{"help", 	no_argument, 		0, 'h' }
	};
	int long_index;
	/* Process option */
	while ((opt = getopt_long(argc, argv,":hi:t:H:p:",
			long_options, &long_index )) != -1) {
		switch (opt) {
		case 'h' :
			printf("using: LedActor --<token> --<id> --<host> --port<>\n"
					"id: guid of the actor\n"
					"token: password of the actor\n"
					"host: mqtt server address, if omitted using local host\n"
					"port: mqtt port, if omitted using port 1883\n");
			return (EXIT_SUCCESS);
			break;
		case 'i':
			guid = StrDup(optarg);
			break;
		case 't':
			token = StrDup(optarg);
			break;
		case 'H':
			host = StrDup(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case ':':
			if (optopt == 'i')
			{
				printf("invalid option(s), using --help for help\n");
				return EXIT_FAILURE;
			}
			break;
		default:
			break;
		}
	}
	if (guid == NULL)
	{
		printf("invalid option(s), using --help for help\n");
		return EXIT_FAILURE;
	}
	option.guid = guid;
	option.psw = token;
	option.host = host;
	option.port = port;
	PowerActorStart(&option);
	return EXIT_SUCCESS;
}


