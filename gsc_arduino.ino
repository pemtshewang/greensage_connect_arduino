#include <WiFi.h>

#include <WebSocketsServer.h>

#include <DHT.h>

#include <PubSubClient.h>

#include <Wire.h>

#include "RTClib.h"

#include <Preferences.h>

#define DHTPIN 4

RTC_DS3231 rtc;
WiFiClient espClient;
PubSubClient client(espClient);
WebSocketsServer webSocket = WebSocketsServer(80);
Preferences prefs;
DHT dht(DHTPIN, DHT22);

//---- HiveMQ Cloud Broker settings
const char * mqtt_server = "192.168.12.82";
const char * mqtt_username = "admin";
const char * mqtt_password = "$10Bpemtshewang";
const int mqtt_port = 1883;
static
String controllerBrokerId = "20cb8c63-c";
unsigned long lastMqttPublishTime = 0;

// pin config
const int lightPin = 2;
const int exFanPin = 13;
const int soilMoisturePin = 34;
const int waterValvePin = 5;
const int rightVentilationRollerShutterPinUp = 12;
const int rightVentilationRollerShutterPinDown = 14;
const int leftVentilationRollerShutterPinUp = 27;
const int leftVentilationRollerShutterPinDown = 26;
// pin config

const char * ssid = "pem";
const char * password = "pem55555";

const int dryValue = 850;
const int wetValue = 200;
bool isFanManuallyOn = false;
bool isWaterValveManuallyOn = false;
bool isWaterValveScheduled = false;
bool rightRollerShutterManuallyOn = false;
bool leftRollerShutterManuallyOn = false;

// soil moisture conversion 
int getMoisturePercentage(int sensorValue) {
  return map(sensorValue, dryValue, wetValue, 0, 100);
}

void clearSlot(int slotNumber) {
  String slotKey = "slot" + String(slotNumber);
  prefs.begin(slotKey.c_str(), false);
  Serial.println("Slot " + String(slotNumber) + " cleared");
  prefs.clear();
  prefs.end();
}

void checkSlot(int slotNumber) {
  String slotKey = "slot" + String(slotNumber);
  prefs.begin(slotKey.c_str(), false);
  int repetitionDays = prefs.getInt("repetitionDays");
  String startTime = prefs.getString("start");
  String endTime = prefs.getString("end");
  prefs.end();

  DateTime now = rtc.now();
  int currentDay = now.dayOfTheWeek();
  char currentTimeStr[9];
  // ignores seconds
  sprintf(currentTimeStr, "%02d:%02d", now.hour(), now.minute());

  bool isCurrentDayIncluded = repetitionDays & (1 << currentDay);

  if (isCurrentDayIncluded) {
    if (startTime <= String(currentTimeStr) && String(currentTimeStr) <= endTime && !isWaterValveScheduled) {
      // Perform actions for this slot
      Serial.println("Slot " + String(slotNumber) + " - Water valve is open");
      digitalWrite(waterValvePin, HIGH);
      isWaterValveScheduled = true;
      // Additional actions for this slot if needed
    }
    if (String(currentTimeStr) > endTime && isWaterValveScheduled) {
      // Additional actions or cleanup for this slot
      Serial.println("Slot " + String(slotNumber) + " - Water valve is closed");
      digitalWrite(waterValvePin, LOW);
      isWaterValveScheduled = false;
    }
  }
}

void storeScheduledDates(int slotNumber, String startTime, String endTime, int repetitionDays) {
  switch (slotNumber) {
  case 1:
    Serial.println("Slot 1 storing");
    prefs.begin("slot1", false);
    prefs.putString("start", startTime);
    prefs.putString("end", endTime);
    prefs.putInt("repetitionDays", repetitionDays);
    prefs.end();
    break;
  case 2:
    Serial.println("Slot 2 storing");
    prefs.begin("slot2", false);
    prefs.putString("start", startTime);
    prefs.putString("end", endTime);
    prefs.putInt("repetitionDays", repetitionDays);
    prefs.end();
    break;
  case 3:
    Serial.println("Slot 3 storing");
    prefs.begin("slot3", false);
    prefs.putString("start", startTime);
    prefs.putString("end", endTime);
    prefs.putInt("repetitionDays", repetitionDays);
    prefs.end();
    break;
  }
}

unsigned long lastReadingTime = 0;

void handleDeviceControl(const String & topic,
  const String & message) {
  if (topic == "light") {
    digitalWrite(lightPin, (message == "on") ? HIGH : LOW);
  } else if (topic == "ventilationFan") {
    Serial.println("Ventilation fan is " + message);
    isFanManuallyOn = message == "on";
    digitalWrite(exFanPin, (message == "on") ? HIGH : LOW);
  } else if (topic == "waterValve") {
    Serial.println("Water valve is " + message);
    isWaterValveManuallyOn = message == "open";
    digitalWrite(waterValvePin, (message == "open") ? HIGH : LOW);
  } else if (topic == "schedule") {
    String scheduleInfo = message.substring(9); // Skip "schedule|"
    int firstDelimiterPos = scheduleInfo.indexOf('|');
    int secondDelimiterPos = scheduleInfo.indexOf('|', firstDelimiterPos + 1);
    int thirdDelimiterPos = scheduleInfo.indexOf('|', secondDelimiterPos + 1);

    String slotNumber = scheduleInfo.substring(0, firstDelimiterPos);
    String startTime = scheduleInfo.substring(firstDelimiterPos + 1, secondDelimiterPos);
    String endTime = scheduleInfo.substring(secondDelimiterPos + 1, thirdDelimiterPos);
    String repetitionDaysStr = scheduleInfo.substring(thirdDelimiterPos + 1);

    int repetitionDays = repetitionDaysStr.toInt(); // Convert repetitionDaysStr to an integer

    Serial.println("Slot: " + slotNumber);
    Serial.println("Start Time: " + startTime);
    Serial.println("End Time: " + endTime);
    Serial.println("Repetition Days: " + repetitionDays);
    // convert slotNumber to int
    storeScheduledDates(slotNumber.toInt(), startTime, endTime, repetitionDays);
  } else if (topic == "scheduleClear") {
    clearSlot(message.toInt());
  } else if (topic == "rollerShutterLeft") {
    if (message == "up") {
      Serial.println("Left roller shutter up");
      leftRollerShutterManuallyOn = true;
      digitalWrite(leftVentilationRollerShutterPinUp, HIGH);
      digitalWrite(leftVentilationRollerShutterPinDown, LOW);
    } else {
      leftRollerShutterManuallyOn = false;
      Serial.println("Left roller shutter down");
      digitalWrite(leftVentilationRollerShutterPinUp, LOW);
      digitalWrite(leftVentilationRollerShutterPinDown, HIGH);
    }
  } else if (topic == "rollerShutterRight") {
    if (message == "up") {
      rightRollerShutterManuallyOn = true;
      Serial.println("Right roller shutter up");
      digitalWrite(rightVentilationRollerShutterPinUp, HIGH);
      digitalWrite(rightVentilationRollerShutterPinDown, LOW);
    } else {
      rightRollerShutterManuallyOn = false;
      Serial.println("Right roller shutter down");
      digitalWrite(rightVentilationRollerShutterPinUp, LOW);
      digitalWrite(rightVentilationRollerShutterPinDown, HIGH);
    }
  }
}

void callback(char * topic, byte * payload, unsigned int length) {
  String receivedTopic = String(topic);
  String receivedPayload;
  for (int i = 0; i < length; i++) {
    receivedPayload += (char) payload[i];
  }
  Serial.println("Received topic: " + receivedTopic);
  int lastSlashIndex = receivedTopic.lastIndexOf('/');
  // the recieved topic will be in the format of "controllerId/deviceId"
  // split and get the deviceId
  Serial.println("Received deviceid: " + receivedTopic.substring(lastSlashIndex + 1));
  Serial.println("Received payload: " + receivedPayload);
  handleDeviceControl(receivedTopic.substring(lastSlashIndex + 1), receivedPayload);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("Connected to WebSocket");
  } else if (type == WStype_TEXT) {
    if (length > 0) {
      String message = (char * ) payload;
      if (message == "ping") {
        // Respond to 'ping' with 'pong'
        webSocket.sendTXT(num, "pong");
        // if the message recieved starts from "threshold:" then update the threshold valueh
      } else if (message.startsWith("threshold:")) {
        // the message will be threshold:temperature:value
        String thresholdType = message.substring(message.indexOf(':') + 1, message.lastIndexOf(':'));
        float thresholdValue = message.substring(message.lastIndexOf(':') + 1).toFloat();
        prefs.begin("my-app", false);
        if (thresholdType == "temperature") {
          prefs.putFloat("temp", thresholdValue);
        } else if (thresholdType == "humidity") {
          prefs.putFloat("hum", thresholdValue);
        } else if (thresholdType == "soil_moisture") {
          prefs.putFloat("soil", thresholdValue);
        }
        prefs.end();
      } else if (message.startsWith("scheduleClear|")) {
        // the message will be scheduleClear|slotNumber
        String slotNumber = message.substring(14); // Skip "scheduleClear|"
        clearSlot(slotNumber.toInt());
      } else if (message.startsWith("schedule|")) {
        String scheduleInfo = message.substring(9); // Skip "schedule|"
        int firstDelimiterPos = scheduleInfo.indexOf('|');
        int secondDelimiterPos = scheduleInfo.indexOf('|', firstDelimiterPos + 1);
        int thirdDelimiterPos = scheduleInfo.indexOf('|', secondDelimiterPos + 1);

        String slotNumber = scheduleInfo.substring(0, firstDelimiterPos);
        String startTime = scheduleInfo.substring(firstDelimiterPos + 1, secondDelimiterPos);
        String endTime = scheduleInfo.substring(secondDelimiterPos + 1, thirdDelimiterPos);
        String repetitionDaysStr = scheduleInfo.substring(thirdDelimiterPos + 1);

        int repetitionDays = repetitionDaysStr.toInt(); // Convert repetitionDaysStr to an integer

        Serial.println("Slot: " + slotNumber);
        Serial.println("Start Time: " + startTime);
        Serial.println("End Time: " + endTime);
        Serial.println("Repetition Days: " + repetitionDays);
        // convert slotNumber to int
        storeScheduledDates(slotNumber.toInt(), startTime, endTime, repetitionDays);
      } else {
        String device = message.substring(0, message.indexOf(':'));
        String action = message.substring(message.indexOf(':') + 1);
        String topic = device;
        handleDeviceControl(topic, action);
      }
    }
  }
}

void reconnect() {
  // Loop until we’re reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection… ");
    String clientId = "ESP32Client";
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected!");
      // subscribe to every topic
      client.subscribe((controllerBrokerId + "/#").c_str());
    } else {
      Serial.print("failed, rc = ");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  if (!rtc.begin()) {
    Serial.println("Could not find RTC! Check circuit.");
    while (1);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  prefs.begin("my-app", false);
  static float defaultThreshold = 25.0;
  prefs.putFloat("temp", defaultThreshold);
  randomSeed(micros());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  WiFi.softAP("ESP32-Access-Point", "123456789");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  pinMode(lightPin, OUTPUT);
  pinMode(exFanPin, OUTPUT);
  pinMode(waterValvePin, OUTPUT);
  pinMode(soilMoisturePin, INPUT);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  // only for mqtts
  // espClient.setCACert(root_ca);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  dht.begin();
  prefs.end();
  delay(500);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  webSocket.loop();
  prefs.begin("my-app", false);
  float tempThreshold = prefs.getFloat("temp");
  float humThreshold = prefs.getFloat("hum");
  prefs.end();

  for (int i = 1; i <= 3; i++) {
    checkSlot(i);
    // You can add delays or other handling if needed between checking slots
    delay(100); // Example delay between slot checks
  }

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (temperature > tempThreshold && !isnan(temperature)) {
    Serial.println("Temperature is greater than threshold");
    digitalWrite(exFanPin, HIGH);
    digitalWrite(rightVentilationRollerShutterPinUp, HIGH);
    digitalWrite(leftVentilationRollerShutterPinUp, HIGH);
    digitalWrite(rightVentilationRollerShutterPinDown, LOW);
    digitalWrite(leftVentilationRollerShutterPinDown, LOW);
  }
  if (humidity > humThreshold && !isnan(humidity)) {
    Serial.println("Humidity is greater than threshold");
    digitalWrite(exFanPin, HIGH);
    digitalWrite(rightVentilationRollerShutterPinUp, HIGH);
    digitalWrite(leftVentilationRollerShutterPinUp, HIGH);
    digitalWrite(rightVentilationRollerShutterPinDown, LOW);
    digitalWrite(leftVentilationRollerShutterPinDown, LOW);
  }

  if (temperature < tempThreshold || humidity < humThreshold) {
    if (!isFanManuallyOn) {
      digitalWrite(exFanPin, LOW);
    }
    if (!leftRollerShutterManuallyOn) {
      digitalWrite(leftVentilationRollerShutterPinUp, LOW);
      digitalWrite(leftVentilationRollerShutterPinDown, HIGH);
    }
    if (!rightRollerShutterManuallyOn) {
      digitalWrite(rightVentilationRollerShutterPinUp, LOW);
      digitalWrite(rightVentilationRollerShutterPinDown, HIGH);
    }
  }

  unsigned long currentTime = millis();
  if (currentTime - lastReadingTime >= 0.5 * 60 * 1000 && webSocket.connectedClients() > 0) {
    // the readings are sent after 30secs from the current elapsed time
    int soilMoisture = getMoisturePercentage(analogRead(soilMoisturePin));
    if (!isnan(temperature) && !isnan(humidity && !isnan(soilMoisture))) {
      webSocket.broadcastTXT("temperature:" + String(temperature));
      webSocket.broadcastTXT("humidity:" + String(humidity));
      webSocket.broadcastTXT("soilMoisture:" + String(soilMoisture));
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }
    lastReadingTime = currentTime;
  }
  if (currentTime - lastMqttPublishTime >= 60 * 1000) {
    int soilMoisture = getMoisturePercentage(analogRead(soilMoisturePin));
    if (!isnan(temperature) && !isnan(humidity) && !isnan(soilMoisture)) {
      String mqttMessage = "temperature:" + String(temperature) +
        "|humidity:" + String(humidity) +
        "|soilMoisture:" + String(soilMoisture);
      client.publish(controllerBrokerId.c_str(), mqttMessage.c_str());
      lastMqttPublishTime = currentTime;
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }
  }
}
