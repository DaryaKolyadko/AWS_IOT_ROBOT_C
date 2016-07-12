#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#define MAX_LENGTH_OF_UPDATE_JSON_BUFFER 200
#define DEFAULT_MESSAGE_LENGTH 40
#define DIRECTION_NUM 4
#define COMMAND_NUM 4

void publishRobotState();
void printField();
void publishMessage(char* message);
char* getFieldAsString();
void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,const char *pReceivedJsonDocument, void *pContextData);

static char* protocol_str[] = {"GO", "STOP", "LEFT", "RIGHT"};
enum protocol {COMMAND_GO, COMMAND_STOP, COMMAND_LEFT, COMMAND_RIGHT};
enum protocol currentCommand;

static char* state_type_str[] = {"STOP", "GO"};
enum state_type {STOP, GO};
enum state_type currentState;

static char* sensor_type_str[] = {"NONE", "WALL", "DEEP", };
enum sensor_type {NONE, WALL, DEEP};
enum sensor_type currentSensor;

static char* direction_type_str[] = {"UP", "RIGHT", "DOWN", "LEFT"}; 
static char direction_type_sign[] = {'^', '>', 'v', '<'};
int shiftList[4][2] = {{-1, 0}, {0, 1}, {1, 0}, {0, -1}}; // for next step checking

enum direction_type {UP, RIGHT, DOWN, LEFT};
enum direction_type currentDirection;

struct matrix {
	int rows;
	int cols;
	int **m;
};

struct matrix field;

struct position {
	int i;
	int j;
};

struct position currentPosition;

struct robotState {
	enum state_type state;
	enum sensor_type sensor;
};

struct robotState currentRobotState;

AWS_IoT_Client mqttClient;
char certDirectory[PATH_MAX + 1] = "./certs";
static char filename[PATH_MAX + 1] = "config.txt";
char HostAddress[255] = AWS_IOT_MQTT_HOST;
uint32_t port = AWS_IOT_MQTT_PORT;

char* publishTopicName;
char* subscribeTopicName;
int qosLevel; 


bool checkNextStep() {
	printField();
	// IOT_INFO("at %d %d %s", currentPosition.i, currentPosition.j, direction_type_str[currentDirection]);
	// IOT_INFO("shift %d %d", shiftList[currentDirection][0],  shiftList[currentDirection][1]);
	// IOT_INFO("next [%d, %d] = %d", currentPosition.i + shiftList[currentDirection][0],
						// currentPosition.j + shiftList[currentDirection][1], field.m[currentPosition.i + shiftList[currentDirection][0]]
						// [currentPosition.j + shiftList[currentDirection][1]]);
	struct position nextPosition;
	nextPosition.i = currentPosition.i + shiftList[currentDirection][0];
	nextPosition.j = currentPosition.j + shiftList[currentDirection][1];

	enum state_type next;

	if (nextPosition.i < 0 || nextPosition.j < 0 || nextPosition.i >= field.cols ||
			nextPosition.j >= field.rows) {
		next = WALL;
	} else {
		next = field.m[currentPosition.i + shiftList[currentDirection][0]]
						[currentPosition.j + shiftList[currentDirection][1]];
	}

	if (next != NONE) {
		char res[25];
		currentSensor = next;

		if (currentState == GO) {
			currentState = STOP;
			sprintf(res, "Stopped: %s", sensor_type_str[currentSensor]);
		} else if (currentState == STOP) {
			sprintf(res, "The %s was detected", sensor_type_str[currentSensor]);
		}

		publishRobotState();
		publishMessage(res);
		return false;
	}

	currentSensor = next;
	return true;
}

void makeStep() {
	if (currentState != STOP) {
		currentPosition.i += shiftList[currentDirection][0];
		currentPosition.j += shiftList[currentDirection][1];
		publishRobotState();

		char* fieldStr = getFieldAsString();
		char* res = (char*) malloc(MAX_LENGTH_OF_UPDATE_JSON_BUFFER*sizeof(char));
		strcpy(res, "");
		strcat(res, "I am going\n");
		strcat(res, fieldStr);
		publishMessage(res);
		free(res);
		// publishMessage("I am going");
		// publishMessage(getFieldAsString());
}
}

void turn(enum protocol where) {
	if (where == COMMAND_LEFT) {
		currentDirection = (currentDirection - 1) % DIRECTION_NUM;
		publishMessage("Turned left");
	} else if (where == COMMAND_RIGHT) {
		currentDirection = (currentDirection + 1) % DIRECTION_NUM;
		publishMessage("Turned right");
	} 
}

static bool readDataFromFile() {
	int i;
	int j;
	FILE *fp;
	publishTopicName = (char*)malloc(sizeof(char)* 100);
	subscribeTopicName = (char*)malloc(sizeof(char)* 100);

	fp = fopen(filename, "rt");

	if (fp == NULL) {
		return false;
	}

	fscanf(fp, "%s", publishTopicName);
	fscanf(fp, "%s", subscribeTopicName);
	fscanf(fp, "%d", &qosLevel);
	fscanf(fp, "%d %d", &field.rows, &field.cols);

	field.m = malloc(field.rows*sizeof(int*));
		
	for (i = 0; i < field.rows; i++) {
		field.m[i] = malloc(field.cols*sizeof(int));
	}

	for (i = 0; i < field.rows; i++){
		for(j = 0; j < field.cols; j++) {
			fscanf(fp, "%d", &field.m[i][j]);
		}
	}

	fscanf(fp, "%d %d", &currentPosition.i, &currentPosition.j);
	fscanf(fp, "%d", &i);
	currentDirection = i % DIRECTION_NUM;
	currentState = STOP;
	printf("direction %s", direction_type_str[currentDirection]);
	fclose(fp);
	return true;
}

char* getFieldAsString() {
	char* fieldStr = (char*) malloc(MAX_LENGTH_OF_UPDATE_JSON_BUFFER);
	strcpy(fieldStr, "");
	int i;
	int j;
	for (i = 0; i < field.rows; i++){
		for(j = 0; j < field.cols; j++) {
			char n[2] = "";

			if (currentPosition.i == i && currentPosition.j == j) {
				snprintf(n, 2, "%c", direction_type_sign[currentDirection]);
			} else {
				if (field.m[i][j] != 0) {
					snprintf(n, 2, "%d", field.m[i][j]);
				} else {
					snprintf(n, 2, "%s", " ");
				}
			}
			
			strcat(fieldStr, n);
		}
		strcat(fieldStr, "\n");
	}

	return fieldStr;
}

void printField() {
	int i;
	int j;
	char* fieldStr = (char*) malloc(MAX_LENGTH_OF_UPDATE_JSON_BUFFER);
	strcpy(fieldStr, "");
	for (i = 0; i < field.rows; i++){
		for(j = 0; j < field.cols; j++) {
			char n[2] = "";

			if (currentPosition.i == i && currentPosition.j == j) {
				snprintf(n, 2, "%c", direction_type_sign[currentDirection]);
			} else {
				if (field.m[i][j] != 0) {
					snprintf(n, 2, "%d", field.m[i][j]);
				} else {
					snprintf(n, 2, "%s", " ");
				}
			}
			strcat(fieldStr, n);
		}
		strcat(fieldStr, "\n");
	}

	IOT_INFO("%s", fieldStr);
	free(fieldStr);
}

void handleCommand() {
	IOT_INFO("current state %d %s", currentState, state_type_str[currentState]);

	if (currentState == GO) {	// GO
		if (currentCommand != COMMAND_STOP) {
			publishMessage("Prohibited command. Only STOP available");
		} else {
			currentState = STOP;
			IOT_INFO("stopped");
			currentSensor = NONE;
			publishRobotState();
			publishMessage("Stopped");
		}
	} 
	else if (currentState == STOP) {				// STOP
		IOT_INFO("current command %d %s", currentCommand, protocol_str[currentCommand]);
		if (currentCommand == COMMAND_LEFT || currentCommand == COMMAND_RIGHT) {
				turn(currentCommand);
				publishRobotState();
				checkNextStep(); 
		} else if (currentCommand == COMMAND_GO) {
				if (checkNextStep()) {
				// 	makeStep();
					currentState = GO;
					// currentSensor = NONE;
					// publishRobotState();
				}
		} else if (currentCommand == COMMAND_STOP) {
			publishMessage("I am not moving");
		}
	}

	if (currentState == STOP) {
		publishMessage(getFieldAsString());
	}
}

bool findCommand(char* command) {
	int i;
	for (i = 0; i < COMMAND_NUM; i++) {
		if (strcmp(command, protocol_str[i]) == 0) {
			currentCommand = i;
			return true;
		}
	}

	currentCommand = STOP;
	return false;
}

void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	IOT_UNUSED(pData);
	IOT_UNUSED(pClient);
	IOT_INFO("Message: %.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char*)params->payload);

	char* msg = (char*)(malloc((int) params->payloadLen) + 1);
	strncpy(msg, (char*) params->payload, (int) params->payloadLen);
	msg[(int) params->payloadLen] = '\0';


	if (findCommand(msg)) {
		handleCommand();
	} else {
		publishRobotState();
		publishMessage("Undefined command");
	}
}

void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) {
	IOT_WARN("MQTT Disconnect");
	IoT_Error_t rc = FAILURE;

	if (NULL == pClient) {
		return;
	}

	IOT_UNUSED(data);

	if (aws_iot_is_autoreconnect_enabled(pClient)) {
		IOT_INFO("Auto Reconnect is enabled, Reconnecting attempt will start now");
	} else {
		IOT_WARN("Auto Reconnect not enabled. Starting manual reconnect...");
		rc = aws_iot_mqtt_attempt_reconnect(pClient);
		if (NETWORK_RECONNECTED == rc) {
			IOT_WARN("Manual Reconnect Successful");
		} else {
			IOT_WARN("Manual Reconnect Failed - %d", rc);
		}
	}
}

void parseInputArgsForConnectParams(int argc, char **argv) {
	int opt;

	while (-1 != (opt = getopt(argc, argv, "h:p:c:t:s:"))) {
		switch (opt) {
			case 'h':
				strcpy(HostAddress, optarg);
				IOT_DEBUG("host %s\n", optarg);
				break;
			case 'p':
				port = atoi(optarg);
				IOT_DEBUG("port %s\n", optarg);
				break;
			case 'c':
				strcpy(certDirectory, optarg);
				IOT_DEBUG("cert root directory %s\n", optarg);
				break;
			case 't':
				strcpy(publishTopicName, optarg);
				IOT_DEBUG("publish topic name: %s\n", optarg);
				break;
			case 's':
				strcpy(subscribeTopicName, optarg);
				IOT_DEBUG("subscribe topic name: %s\n", optarg);
				break;
			case '?':
				if (optopt == 'c') {
					IOT_ERROR("Option -%c requires an argument.", optopt);
				} else if (isprint(optopt)) {
					IOT_WARN("Unknown option `-%c'.", optopt);
				} else {
					IOT_WARN("Unknown option character `\\x%x'.", optopt);
				}
				break;
			default:
				IOT_ERROR("Error in command line argument parsing");
				break;
		}
	}

}

void publishMessage(char* message) {
	IoT_Error_t rc = FAILURE;
	IoT_Publish_Message_Params paramsQOS0;
	paramsQOS0.qos = QOS0;
	paramsQOS0.payload = (void *) message;
	paramsQOS0.isRetained = 0;

	while (NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS != rc) {
		//Max time the yield function will wait for read messages
		rc = aws_iot_mqtt_yield(&mqttClient, 1000);

		if (NETWORK_ATTEMPTING_RECONNECT == rc) {
			// If the client is attempting to reconnect we will skip the rest of the loop.
			continue;
		}

		paramsQOS0.payloadLen = strlen(message);

		do {
			rc = aws_iot_mqtt_publish(&mqttClient, publishTopicName, strlen(publishTopicName), &paramsQOS0);
		} while (MQTT_REQUEST_TIMEOUT_ERROR == rc);
	}

	if (SUCCESS != rc) {
		IOT_ERROR("An error occurred while publishing message.\n");
	}	
}

void publishRobotState() {
	IoT_Error_t rc = FAILURE;
	int32_t i = 0;

	char jsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
	size_t sizeOfJsonDocumentBuffer = sizeof(jsonDocumentBuffer) / sizeof(jsonDocumentBuffer[0]);
	char *pJsonStringToUpdate;

	jsonStruct_t stateHandler;
	stateHandler.cb = NULL;
	stateHandler.pData = state_type_str[currentState];
	stateHandler.pKey = "robotState";
	stateHandler.type = SHADOW_JSON_STRING;


	jsonStruct_t sensorHandler;
	sensorHandler.cb = NULL;
	sensorHandler.pData = sensor_type_str[currentSensor];
	sensorHandler.pKey = "sensorState";
	sensorHandler.type = SHADOW_JSON_STRING;

	jsonStruct_t directionHandler;
	directionHandler.cb = NULL;
	directionHandler.pKey = "direction";
	directionHandler.pData = direction_type_str[currentDirection];
	directionHandler.type = SHADOW_JSON_STRING;

	jsonStruct_t iHandler;
	iHandler.cb = NULL;
	iHandler.pKey = "i";
	iHandler.pData = &currentPosition.i;
	iHandler.type = SHADOW_JSON_INT32;

	jsonStruct_t jHandler;
	jHandler.cb = NULL;
	jHandler.pKey = "j";
	jHandler.pData = &currentPosition.j;
	jHandler.type = SHADOW_JSON_INT32;

	rc = aws_iot_shadow_init_json_document(jsonDocumentBuffer, sizeOfJsonDocumentBuffer);
		if (SUCCESS == rc) {
			rc = aws_iot_shadow_add_reported(jsonDocumentBuffer, sizeOfJsonDocumentBuffer, 5, 
				&stateHandler, &sensorHandler, &directionHandler, &iHandler, &jHandler);
			if (SUCCESS == rc) {
				rc = aws_iot_finalize_json_document(jsonDocumentBuffer, sizeOfJsonDocumentBuffer);
				if (SUCCESS == rc) {
					IOT_INFO("Update Shadow: %s", jsonDocumentBuffer);
					rc = aws_iot_shadow_update(&mqttClient, AWS_IOT_MY_THING_NAME, jsonDocumentBuffer,
											   ShadowUpdateStatusCallback, NULL, 4, true);
				}
			}
		}
}

void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,const char *pReceivedJsonDocument, void *pContextData) {
	IOT_UNUSED(pThingName);
	IOT_UNUSED(action);
	IOT_UNUSED(pReceivedJsonDocument);
	IOT_UNUSED(pContextData);

	if (SHADOW_ACK_TIMEOUT == status) {
		IOT_INFO("Update Timeout--");
	} else if (SHADOW_ACK_REJECTED == status) {
		IOT_INFO("Update RejectedXX");
	} else if (SHADOW_ACK_ACCEPTED == status) {
		IOT_INFO("Update Accepted!!!");
	}
}

bool connectToThingAndSubscribeToTopic(int argc, char **argv) {
	IoT_Error_t rc = FAILURE;
	char rootCA[PATH_MAX + 1];
	char clientCRT[PATH_MAX + 1];
	char clientKey[PATH_MAX + 1];
	char CurrentWD[PATH_MAX + 1];
	IoT_Publish_Message_Params paramsQOS0;
	getcwd(CurrentWD, sizeof(CurrentWD));
	snprintf(rootCA, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_ROOT_CA_FILENAME);
	snprintf(clientCRT, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_CERTIFICATE_FILENAME);
	snprintf(clientKey, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_PRIVATE_KEY_FILENAME);
	char JsonDocumentBuffer[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
	size_t sizeOfJsonDocumentBuffer = sizeof(JsonDocumentBuffer) / sizeof(JsonDocumentBuffer[0]);
	char *pJsonStringToUpdate;
	ShadowInitParameters_t sp = ShadowInitParametersDefault;
	IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
	
	sp.pHost = AWS_IOT_MQTT_HOST;
	sp.port = AWS_IOT_MQTT_PORT;
	sp.pClientCRT = clientCRT;
	sp.pClientKey = clientKey;
	sp.pRootCA = rootCA;
	sp.enableAutoReconnect = false;
	sp.disconnectHandler = NULL;
	IOT_INFO("Shadow Init");
	rc = aws_iot_shadow_init(&mqttClient, &sp);

	if (SUCCESS != rc) {
		IOT_ERROR("Shadow Connection Error");
		return false;
	}

	ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
	IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;
	scp.pMyThingName = AWS_IOT_MY_THING_NAME;
	scp.pMqttClientId = AWS_IOT_MQTT_CLIENT_ID;
	scp.mqttClientIdLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);

	IOT_INFO("Shadow Connect");
	rc = aws_iot_shadow_connect(&mqttClient, &scp);

	if (SUCCESS != rc) {
		IOT_ERROR("Shadow Connection Error");
		return false;
	}

	mqttInitParams.enableAutoReconnect = false; // We enable this later below
	mqttInitParams.pHostURL = HostAddress;
	mqttInitParams.port = port;
	mqttInitParams.pRootCALocation = rootCA;
	mqttInitParams.pDeviceCertLocation = clientCRT;
	mqttInitParams.pDevicePrivateKeyLocation = clientKey;
	mqttInitParams.mqttCommandTimeout_ms = 20000;
	mqttInitParams.tlsHandshakeTimeout_ms = 5000;
	mqttInitParams.isSSLHostnameVerify = true;
	mqttInitParams.disconnectHandler = disconnectCallbackHandler;
	mqttInitParams.disconnectHandlerData = NULL;

	rc = aws_iot_mqtt_init(&mqttClient, &mqttInitParams);

	if (SUCCESS != rc) {
		IOT_ERROR("aws_iot_mqtt_init returned error : %d ", rc);
		return rc;
	}

	connectParams.keepAliveIntervalInSec = 10;
	connectParams.isCleanSession = true;
	connectParams.MQTTVersion = MQTT_3_1_1;
	connectParams.pClientID = AWS_IOT_MQTT_CLIENT_ID;
	connectParams.clientIDLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);
	connectParams.isWillMsgPresent = false;

	IOT_INFO("Connecting...");
	rc = aws_iot_mqtt_connect(&mqttClient, &connectParams);
	
	if (SUCCESS != rc) {
		IOT_ERROR("Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
		return rc;
	}

	// Enable Auto Reconnect functionality
	rc = aws_iot_shadow_set_autoreconnect_status(&mqttClient, true);

	if (SUCCESS != rc) {
		IOT_ERROR("Unable to set Auto Reconnect to true - %d", rc);
		return false;
	}

	rc = aws_iot_mqtt_autoreconnect_set_status(&mqttClient, true);
	
	if (SUCCESS != rc) {
		IOT_ERROR("Unable to set Auto Reconnect to true - %d", rc);
		return false;
	}

	IOT_INFO("Subscribing...");
	rc = aws_iot_mqtt_subscribe(&mqttClient, subscribeTopicName, strlen(subscribeTopicName), QOS0, iot_subscribe_callback_handler, NULL);
	
	if (SUCCESS != rc) {
		IOT_ERROR("Error subscribing : %d ", rc);
		return false;
	}

	return true;
}


int main(int argc, char **argv) {
	IoT_Error_t rc = FAILURE;
	// IoT_Error_t rc1 = FAILURE;
	parseInputArgsForConnectParams(argc, argv);

	if (!readDataFromFile()) {
		printf("%s not found. Robot cannot work without this configuration.\n", filename);
		return -1;
	} 
	else {
		if (connectToThingAndSubscribeToTopic(argc, argv)) {
			publishMessage(getFieldAsString());
			publishRobotState();

			while (true) {
				//Max time the yield function will wait for read messages
				rc = aws_iot_mqtt_yield(&mqttClient, 1000);
				// rc1 = aws_iot_shadow_yield(&mqttClient, 1000);

				if (NETWORK_ATTEMPTING_RECONNECT == rc) { // || NETWORK_ATTEMPTING_RECONNECT == rc1) {
				// If the client is attempting to reconnect we will skip the rest of the loop.
					continue;
				}

				sleep(3);

				if (currentState == GO) {
					if (checkNextStep()) {
						currentState = GO;
						currentSensor = NONE;
						makeStep();
					} 
				}
			}
		}

		return 0;
	}
}