## Project ESP32 FreeRTOS cJSON MQTT Mosquitto

#The project is divided in four tasks:

- vSensorTask: Read the sensor DHT22, create a JSON struct (using cJSON), like below, and send by queue this message JSON to vPublishTask.

{
   "ID":   "DallaValle_ESP32_LEITURA_SENSOR",
   "MSG":  295,
   "TEMP": 24,
   "UMID": 50
}

- vLedTask: Read the led status, create a JSON struct (using cJSON), like below, and send by queue this message JSON to vPublishTask.

{
   "ID":   "DallaValle_ESP32_LEITURA_LED",
   "MSG":  338,
   "LED_Status":   "ON"
}

- vSwitchTask: Read the tactil switch status, create a JSON struct (using cJSON), like below, and send by queue this message JSON to vPublishTask.

{
   "ID":   "DallaValle_ESP32_LEITURA_SWITCH",
   "MSG":  343,
   "Switch_Status":        "FALSE"
}

- vPublishTask: Receive all the queues and publish with MQTT mosquitto the packages (LEITURA_LED , LEITURA SWITCH e LEITURA SENSOR).
