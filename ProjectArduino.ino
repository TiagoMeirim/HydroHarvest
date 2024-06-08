#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <limits.h>
WebServer server(80);

StaticJsonDocument<1024> jsonDocument;
char buffer[1024];

/* Change these values based on your calibration values */
int lowerThreshold = 1300;
int upperThreshold = 1600;

float humidityLevel;
float tankLevel;
float lightLevel;
float temperatureLevel;
float humiditySum = 0;
float tankLevelSum = 0;
float lightLevelSum = 0;
float temperatureSum = 0;
int readingCount = 0;
int watering;

// Sensor pins
#define sensorWaterPower 2
#define sensorWaterPin 35
#define sensorHumidityPower 0
#define sensorHumidyPin 34
#define sensorLightPin 36
#define sensorTemperaturePin 39
#define buttonPin 33
#define relayPin 13

int lastRelayState = HIGH;
int currentState;

int red = 25; //this sets the red led pin
int green = 26; //this sets the green led pin
int blue = 27; //this sets the blue led pin

unsigned long measureDelay = 3000;                //    NOT LESS THAN 2000!!!!!   
unsigned long lastTimeRan;

const char* ssid = "AutoConnect";
const char* password = "password";

unsigned int Actual_Millis, Previous_Millis;
int refresh_time = 300000;

int minHumidity = -1;
int maxHumidity = INT_MAX;
int minTemperature = -1;
int maxTemperature = INT_MAX;

int startTimeWattering;
int endTimeWattering;

int startMillis;

bool hasSchedule = false;

String plantation;

IPAddress local_IP(192, 168, 1, 125);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);


void setup() {
  // Serial port is activated
  Serial.begin(115200);
  // This delay gives the chance to wait for a Serial Monitor without blocking if none is found
  delay(1500); 

  // Manage the wifi connection
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.

  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  // wm.resetSettings();


  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

  bool res;
  res = wm.autoConnect(ssid, password);

  if(!res) {
      Serial.println("Failed to connect");
      ESP.restart();
  } else {
      //if you get here you have connected to the WiFi    
      Serial.println("Connected...yeey :)");
      Serial.println(WiFi.localIP());
  }

  Actual_Millis = millis();               //Save time for refresh loop
  Previous_Millis = Actual_Millis;

  setupApi();
  digitalWrite(relayPin, HIGH);
  pinMode(relayPin, OUTPUT);
  pinMode(sensorWaterPower, OUTPUT);
  digitalWrite(sensorWaterPin, LOW);
  pinMode(sensorHumidityPower, OUTPUT);
  digitalWrite(sensorHumidyPin, LOW);
  digitalWrite(sensorLightPin, LOW);
  digitalWrite(sensorTemperaturePin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(blue, OUTPUT);
}


void loop() {
  server.handleClient();

  if (millis() > lastTimeRan + measureDelay)  {
    readSensors();
  }

  currentState = digitalRead(buttonPin);
  if(currentState == LOW && lastRelayState == HIGH){
    turnPumpOn();
  }
  else if(currentState == LOW && lastRelayState == LOW) {
    turnPumpOff();
    hasSchedule = false;
  }
  int waterLevel = readWaterLevelSensor();
  //Need to invert for when connected with battery
  if (waterLevel >= 0 && waterLevel <= lowerThreshold) {
    analogWrite(red, 0);
    analogWrite(green, 255);
    analogWrite(blue, 255);
  }
  else if (waterLevel > lowerThreshold && waterLevel <= upperThreshold) {
    analogWrite(red, 0);
    analogWrite(green, 0);
    analogWrite(blue, 255);
  }
  else if (waterLevel > upperThreshold) {
    analogWrite(red, 255);
    analogWrite(green, 0);
    analogWrite(blue, 255);
  }

  /*if (waterLevel >= 0 && waterLevel <= lowerThreshold) {
    analogWrite(red, 255);
    analogWrite(green, 0);
    analogWrite(blue, 0);
  }
  else if (waterLevel > lowerThreshold && waterLevel <= upperThreshold) {
    analogWrite(red, 255);
    analogWrite(green, 255);
    analogWrite(blue, 0);
  }
  else if (waterLevel > upperThreshold) {
    analogWrite(red, 0);
    analogWrite(green, 255);
    analogWrite(blue, 0);
  }*/

  Actual_Millis = millis();
  if(Actual_Millis - Previous_Millis > refresh_time){
    Previous_Millis = Actual_Millis;  
    if(WiFi.status() == WL_CONNECTED){
      HTTPClient http;

      //Begin new connection to website       
      http.begin("http://20.170.64.240:8080/ubiquitous/addData"); // Will get the millis for the watering
      http.addHeader("Content-Type", "application/json");

      getValuesHTTP();

      int response_code = http.POST(buffer);
      
      //If response body -1 dont water

      //If the code is higher than 0, it means we received a response
      if(response_code > 0){
        Serial.println("HTTP code " + String(response_code));
        if(response_code == 202){
          String response_body = http.getString();
          StaticJsonDocument<1024> doc;
          DeserializationError error = deserializeJson(doc, response_body);
          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
          }
          startTimeWattering = doc["start"];
          endTimeWattering = doc["end"];
          if(startTimeWattering > -1 || endTimeWattering > -1){
            hasSchedule = true;
            startMillis = millis();
          }
        }
      }
      else{
       Serial.print("Error sending POST, code: ");
       Serial.println(response_code);
      }
      http.end();
    }
    else{
      Serial.println("WIFI connection error");
      executeWhileOffline();
    }
  }

  if(hasSchedule){
    if(millis() - startMillis >= startTimeWattering && millis() - startMillis <= endTimeWattering){
      turnPumpOn();
    } else {
      turnPumpOff();
      hasSchedule = false;
    }
  }

  delay(1000);
}

void executeWhileOffline(){// Problem should this be like turn pump on for 10 seconds?
  if(humidityLevel<minHumidity || humidityLevel>maxHumidity)
    turnPumpOn();
  else
    turnPumpOff();
}

void turnPumpOn(){
  digitalWrite(relayPin, LOW);
  lastRelayState = LOW;
}

void turnPumpOff(){
  digitalWrite(relayPin, HIGH);
  lastRelayState = HIGH;
}

void readSensors(){
  humidityLevel = readHumidityLevelSensor();
  tankLevel = readWaterLevelSensor();
  lightLevel = readLightLevelSensor();
  temperatureLevel = readTemperatureLevelSensor();
  Serial.println(humidityLevel);
  Serial.println(tankLevel);
  Serial.println(lightLevel);
  Serial.println(temperatureLevel);
  humiditySum += humidityLevel;
  tankLevelSum += tankLevel;
  lightLevelSum += lightLevel;
  temperatureSum += temperatureLevel;
  readingCount++;
  lastTimeRan = millis();
}

float readWaterLevelSensor() {
  digitalWrite(sensorWaterPower, HIGH);
  delay(10);
  int val = analogRead(sensorWaterPin);
  digitalWrite(sensorWaterPower, LOW);
  return val;
}

float readHumidityLevelSensor() {
  digitalWrite(sensorHumidityPower, HIGH);
  delay(10);
  int val = analogRead(sensorHumidyPin);
  digitalWrite(sensorHumidityPower, LOW);
  return val;
}

float readLightLevelSensor() {
  int val = analogRead(sensorLightPin);
  return calculateLightPercentage(val);
}

float calculateLightPercentage(int sensorValue){
  return (sensorValue / 4095.0) * 100.0;
}

float readTemperatureLevelSensor() {
  int val = analogRead(sensorTemperaturePin);
  return calculateTemperature(val);
}

// Function to calculate temperature in Celsius
float calculateTemperature(int tempVal) {
  double voltage = (float) tempVal / 4095.0 * 3.3;
  double Rt = 10 * voltage / (3.3 - voltage);
  double tempk = 1 / (1 / (273.15 + 25) + log(Rt / 10) / 3950.0);
  double tempC = tempk - 273.15;
  return tempC;
}


void handleStartWatering() {
  turnPumpOn();

  // Respond to the client
  server.send(200, "application/json", "{}");
}

void handleStopWatering() {
  turnPumpOff();

  // Respond to the client
  server.send(200, "application/json", "{}");
}

void createJson(char *name, float value, char *unit) {  
  jsonDocument.clear();
  jsonDocument["name"] = name;
  jsonDocument["value"] = value;
  jsonDocument["unit"] = unit;
  serializeJson(jsonDocument, buffer);  
}
 
void addJsonObject(JsonArray& array, char *name, float value, char *unit) {
  JsonObject obj = array.createNestedObject();
  obj["name"] = name;
  obj["value"] = value;
  obj["unit"] = unit; 
}

void getValues() {
  Serial.println("Get all values");
  jsonDocument.clear(); // Clear json buffer
  jsonDocument["ip"] = WiFi.localIP();
  JsonArray sensors = jsonDocument.createNestedArray("sensors");
  humidityLevel = humiditySum/readingCount;
  tankLevel = tankLevelSum/readingCount;
  lightLevel = lightLevelSum/readingCount;
  temperatureLevel = temperatureSum/readingCount;
  addJsonObject(sensors, "humidityLevel", humidityLevel, "%");
  addJsonObject(sensors, "tankLevel", tankLevel, "%");
  addJsonObject(sensors, "lightLevel", lightLevel, "%");
  addJsonObject(sensors, "temperatureLevel", temperatureLevel, "°C");

  serializeJson(jsonDocument, buffer);
  server.send(200, "application/json", buffer);
}

char* getValuesHTTP() {
  Serial.println("Get all values");
  jsonDocument.clear(); // Clear json buffer
  jsonDocument["ip"] = WiFi.localIP();
  JsonArray sensors = jsonDocument.createNestedArray("sensors");
  humidityLevel = humiditySum/readingCount;
  humiditySum = 0;
  tankLevel = tankLevelSum/readingCount;
  tankLevelSum = 0;
  lightLevel = lightLevelSum/readingCount;
  lightLevelSum = 0;
  temperatureLevel = temperatureSum/readingCount;
  temperatureSum = 0;
  readingCount = 0;
  addJsonObject(sensors, "humidityLevel", humidityLevel, "%");
  addJsonObject(sensors, "tankLevel", tankLevel, "%");
  addJsonObject(sensors, "lightLevel", lightLevel, "%");
  addJsonObject(sensors, "temperatureLevel", temperatureLevel, "°C");

  serializeJson(jsonDocument, buffer);
  return buffer;
}

void setPlantation(){

  if (server.hasArg("plain") == false) {
    //handle error here
  }
  String body = server.arg("plain");
  deserializeJson(jsonDocument, body);
  
  plantation = jsonDocument["plantation"].as<String>();

  if(WiFi.status() == WL_CONNECTED){
      HTTPClient http;

      //Begin new connection to website       
      http.begin("http://20.170.64.240:8080/ubiquitous/getInformation?plant=" + plantation); // Plantation name provided by the application
      http.addHeader("Content-Type", "application/json");

      int response_code = http.GET();
      
      //If response body -1 dont water

      //If the code is higher than 0, it means we received a response
      if(response_code > 0){
        Serial.println("HTTP code " + String(response_code));
        if(response_code == 202){
          String response_body = http.getString();
          StaticJsonDocument<1024> doc;
          DeserializationError error = deserializeJson(doc, response_body);
          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
          }
          minHumidity = doc["minHumidity"];
          maxHumidity = doc["maxHumidity"];
          maxTemperature = doc["maxTemperature"];
          minTemperature = doc["minTemperature"];
        }
      }
      else{
       Serial.print("Error sending GET, code: ");
       Serial.println(response_code);
      }
      http.end();
    }
    else{
      Serial.println("WIFI connection error");
    }
}

void setupApi() {
  server.on("/getValues", getValues);
  server.on("/setStartWatering", HTTP_POST, handleStartWatering);
  server.on("/setStopWatering", HTTP_POST, handleStopWatering);
  server.on("/setPlantation", HTTP_POST, setPlantation);

 
  //start server
  server.begin();
}
