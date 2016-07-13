# AWS_IOT_ROBOT_C
AWS IOT Robot. Connect to AWS IoT cloud and save its state there. Interact with Remote Controller by publishing\reading messages in topics.

##Prerequisities
- Download AWS IoT SDK Embedded C [here](https://github.com/aws/aws-iot-device-sdk-embedded-C)
- Download certificate authority CA file from [Symantec](https://www.symantec.com/content/en/us/enterprise/verisign/roots/VeriSign-Class%203-Public-Primary-Certification-Authority-G5.pem) (root certificate)
- Ensure you have created a thing through your AWS IoT Console with name matching the definition AWS_IOT_MY_THING_NAME in the ```aws_iot_config.h``` file
- Ensure the names of the cert files are the same as in the ```aws_iot_config.h``` file
- Ensure the certificate has an attached policy which allows the proper permissions for AWS IoT

##  Create a thing through AWS IoT Console
1. AWS IoT Console - https://console.aws.amazon.com/iot/home
2. Create a Thing, Create and Activate a Thing Certificate, Create an AWS IoT Policy and attach it to a device (or just click "Connect a device" button and then certificate and policy will be generated automatically) [Tutorial](http://docs.aws.amazon.com/iot/latest/developerguide/create-device.html)

###Connecting to the AWS IoT MQTT platform

Connecting to the AWS IoT MQTT platform

```
AWS_IoT_Client client;
rc = aws_iot_mqtt_init(&client, &iotInitParams);
rc = aws_iot_mqtt_connect(&client, &iotConnectParams);
```


Subscribe to a topic

```
AWS_IoT_Client client;
rc = aws_iot_mqtt_subscribe(&client, "sdkTest/sub", 11, QOS0, iot_subscribe_callback_handler, NULL);
```


Update Thing Shadow from a device

``` 
rc = aws_iot_shadow_update(&mqttClient, AWS_IOT_MY_THING_NAME, pJsonDocumentBuffer, ShadowUpdateStatusCallback,
                            pCallbackContext, TIMEOUT_4SEC, persistenSubscription);
```
## Resources
[API Documentation](http://aws-iot-device-sdk-embedded-c-docs.s3-website-us-east-1.amazonaws.com/index.html)

###Config
Need a config file to be started. Content:
- topic to publish (sent messages)
- topic to subscribe (get messages)
- QOS (0 | 1)
- field size (width, height)
- field (matrix)
- initial position: [i, j]
- initial direction (UP = 0, RIGHT = 1, DOWN = 2, LEFT = 3)

Example:
```
CKolyadkoTopic JavaKolyadkoTopic 0
10 10
1 1 1 1 1 1 1 1 1 1
1 0 0 0 1 0 0 0 0 1
1 0 0 0 1 0 0 0 0 1
1 0 2 2 1 0 0 0 0 1
1 0 2 2 1 0 0 0 0 1
1 0 0 0 0 0 1 1 1 1
1 0 2 2 0 0 1 0 0 1
1 0 2 2 0 0 1 0 0 1
1 0 0 0 0 0 0 0 0 1
1 1 1 1 1 1 1 1 1 1
8 8
2
```

##Running
1. Build the project using Makefile(```make```)
2. Run the project 
```
./RobotIotApp -f CONFIG_FIELD_FILE
```
