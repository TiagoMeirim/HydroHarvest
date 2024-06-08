#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <WiFi.h>

WebServer server(80);
//WiFiServer server(80);

StaticJsonDocument<1024> jsonDocument;
char buffer[1024];

/* Change these values based on your calibration values */
int lowerThreshold = 1300;
int upperThreshold = 1850;

float humidityLevel;
float tankLevel;
float lightLevel;
float temperatureLevel;
int watering;

// Sensor pins
#define sensorWaterPower 2
#define sensorWaterPin 34
#define sensorHumidityPower 0
#define sensorHumidyPin 35
#define sensorLightPin 36
#define sensorTemperaturePin 39 
#define buttonPin 33
#define relayPin 13

bool lastState = false;
int currentState;

int red = 25; //this sets the red led pin
int green = 26; //this sets the green led pin
int blue = 27; //this sets the blue led pin

unsigned long measureDelay = 3000;                //    NOT LESS THAN 2000!!!!!   
unsigned long lastTimeRan;

IPAddress local_IP(192,168,1,124);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
IPAddress primaryDNS(8,8,8,8);
IPAddress secondaryDNS(8,8,4,4);
const char *ssid = "Thomson388369";
const char *password = "347D8C3016";

void setup() {
  //Serial.begin(9600);
  // Serial port is activated
  Serial.begin(115200);
  // This delay gives the chance to wait for a Serial Monitor     without blocking if none is found
  delay(1500); 

  // Manage the wifi connection
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.

  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if(!res) {
      Serial.println("Failed to connect");
      ESP.restart();
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("Connected...yeey :)");
  }

  if(!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS))
  {
    Serial.println("STA Failed to configure");
  }
  setupApi();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

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
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);
}

void loop() {
  server.handleClient();

  if (millis() > lastTimeRan + measureDelay)  {
    humidityLevel = readHumidityLevelSensor();
    tankLevel = readWaterLevelSensor();
    lightLevel = readLightLevelSensor();
    temperatureLevel = readTemperatureLevelSensor();

    lastTimeRan = millis();
  }
  currentState = digitalRead(buttonPin);
  if(currentState == LOW){
    lastState = !lastState;
    if(lastState){
      digitalWrite(relayPin, LOW);
    }
    else {
      digitalWrite(relayPin, HIGH);
    }
  }
  if (tankLevel >= 0 && tankLevel <= lowerThreshold) {
    analogWrite(red, 0);
    analogWrite(green, 255);
    analogWrite(blue, 255);
  }
  else if (tankLevel > lowerThreshold && tankLevel <= upperThreshold) {
    analogWrite(red, 0);
    analogWrite(green, 0);
    analogWrite(blue, 255);
  }
  else if (tankLevel > upperThreshold) {
    analogWrite(red, 255);
    analogWrite(green, 0);
    analogWrite(blue, 255);
  }
  delay(1000);
}

float readWaterLevelSensor() {
  digitalWrite(sensorWaterPower, HIGH);
  delay(10);
  int val = analogRead(sensorWaterPin);
  digitalWrite(sensorWaterPower, LOW);
  //Serial.println(val);
  return val;
}

float readHumidityLevelSensor() {
  digitalWrite(sensorHumidityPower, HIGH);
  delay(10);
  int val = analogRead(sensorHumidyPin);
  digitalWrite(sensorHumidityPower, LOW);
  //Serial.println(val);
  return val;
}

float readLightLevelSensor() {
  int val = analogRead(sensorLightPin);
  //Serial.println(val);
  return val;
}

float readTemperatureLevelSensor() {
  float val = analogRead(sensorTemperaturePin);
  /*Serial.print("Temperature: ");
  Serial.print(calculateTemperature(val));
  Serial.println(" °C");*/
  float temp = calculateTemperature(val);
  return temp;
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
  if (server.hasArg("plain") == false) {
    //handle error here
  }
  String body = server.arg("plain");
  deserializeJson(jsonDocument, body);
  
  watering = jsonDocument["watering"];
  digitalWrite(relayPin, LOW);
  // Respond to the client
  server.send(200, "application/json", "{}");
}

void handleStopWatering() {
  if (server.hasArg("plain") == false) {
    //handle error here
  }
  String body = server.arg("plain");
  deserializeJson(jsonDocument, body);
  digitalWrite(relayPin, HIGH);
  watering = jsonDocument["watering"];

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
 
void addJsonObject(char *name, float value, char *unit) {
  JsonObject obj = jsonDocument.createNestedObject();
  obj["name"] = name;
  obj["value"] = value;
  obj["unit"] = unit; 
}

void getValues() {
  Serial.println("Get all values");
  jsonDocument.clear(); // Clear json buffer
  addJsonObject("humidityLevel", humidityLevel, "%");
  addJsonObject("tankLevel", tankLevel * 100.0 / 1800.0  , "%");
  addJsonObject("lightLevel", lightLevel * 100.0 / 4000.0, "%");
  addJsonObject("temperatureLevel", temperatureLevel, "ºC");

  serializeJson(jsonDocument, buffer);
  server.send(200, "application/json", buffer);
}

void setupApi() {
  server.on("/getValues", getValues);
  server.on("/setStartWatering", HTTP_POST, handleStartWatering);
  server.on("/setStopWatering", HTTP_POST, handleStopWatering);

 
  // start server
  server.begin();
}