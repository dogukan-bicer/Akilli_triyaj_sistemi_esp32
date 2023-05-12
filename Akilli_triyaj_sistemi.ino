#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#include <FirebaseESP32.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#include <FirebaseESP8266.h>
#endif
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#define WIFI_SSID "*********"
#define WIFI_PASSWORD "*******"
#define API_KEY "*********"
#define DATABASE_URL "***********"  //<databaseName>.firebaseio.com or <databaseName>.<region>.firebasedatabase.app
#define USER_EMAIL "*********"
#define USER_PASSWORD "*********"
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;
unsigned long count = 0;
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30100_PulseOximeter.h"
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


Adafruit_MLX90614 mlx = Adafruit_MLX90614();

const int buttonPin = 19;  // the number of the pushbutton pin
const int ledPin = 2;      // the number of the LED pin
float temp_amb;
float temp_obj;
float temp_spo2;
float temp_bpm;
float calibration = 2.36;
int32_t LastReport_Id=0, temp_id = 0,temp_id_old=0;
int buttonState;
uint32_t LastReport_pulseoximeter, LastReport_temperature, LastReport_Screen;


PulseOximeter pox;

TaskHandle_t GetReadings;
TaskHandle_t PostToFirebase;
TaskHandle_t Pulse_oximeter_Read;

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);
// set the data rate for the sensor serial port

int getFingerprintIDez() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return 0;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return 0;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return 0;

  // found a match!
  // Serial.print("Found ID #"); Serial.print(finger.fingerID);
  // Serial.print(" with confidence of "); Serial.println(finger.confidence);
  return finger.fingerID;
}

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.begin(115200);
  mlx.begin();
  Serial2.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Address 0x3D for 128x64
    Serial.println(F("SSD1306 Baslatilamadi"));
    for (;;)
      ;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.print("Parmak izi sensoru kaydi");

  finger.begin(57600);
  if (finger.verifyPassword()) {
    display.println("Parmak izi sensoru bulundu!");
  } else {
    display.println("Parmak izi sensoru bulunamadi :(");
    while (1) { delay(1); }
  }
  display.display();
  delay(1000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.print("Sunucuya baglaniyor");
  display.display();
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    display.print(".");
    display.display();
    delay(300);
  }
  display.println();
  display.print("IP ile bağlandi: ");
  display.println(WiFi.localIP());
  display.println();
  display.printf("Firebase İstemcisi v%s\n\n", FIREBASE_CLIENT_VERSION);
  delay(1000);

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;  // see addons/TokenHelper.h
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Firebase.setDoubleDigits(5);
  Serial.println("Sicaklik Sensoru MLX90614");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setCursor(0, 0);
  display.println("pals oksimetre hazirlaniyor..");
  display.display();
  Serial.print("Initializing pulse oximeter..");
  delay(1000);
  if (!pox.begin()) {
    Serial.println("FAILED");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(1);
    display.setCursor(0, 0);
    display.println("BASARSIZ!");
    display.display();
    for (;;)
      ;
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(1);
    display.setCursor(0, 0);
    display.println("HAZİR");
    display.display();
    Serial.println("SUCCESS");
  }

  xTaskCreatePinnedToCore(SensorReadings, "GetReadings", 8192, NULL, 3, &GetReadings, 1);

  xTaskCreatePinnedToCore(Oximeter_read, "Pulse_oximeter_Read", 8192, NULL, 2, &Pulse_oximeter_Read, 0);

  xTaskCreatePinnedToCore(SendReadingsToFirebase, "PostToFirebase", 8192, NULL, 1, &PostToFirebase, 1);
}

void loop() {
  delay(1);
}

void Oximeter_read(void* parameter) {
  for (;;) {
    pox.update();
    if (millis() - LastReport_pulseoximeter > 50) {
      temp_bpm = pox.getHeartRate();
      temp_spo2 = pox.getSpO2();
      LastReport_pulseoximeter = millis();
    }
    vTaskDelay(1);
  }
}

void SensorReadings(void* parameter) {
  for (;;) {

    if (millis() - LastReport_Id > 1) {
      temp_id = getFingerprintIDez();
      LastReport_Id = millis();
      if((temp_id!=temp_id_old)&&(temp_id!=0)){
        temp_id_old=temp_id;
      }
    }

    if (millis() - LastReport_Screen > 310) {
      display.clearDisplay();

      Serial.print("Room Temp = ");
      Serial.println(temp_amb);
      Serial.print("Object temp = ");
      Serial.println(temp_obj);

      display.clearDisplay();
      display.setCursor(25, 0);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.println(" Triyaj sistemi");

      display.setCursor(10, 10);
      display.setTextSize(1);
      display.print("Cevre: ");
      display.print(temp_amb);
      display.print((char)247);
      display.print("C");

      display.setCursor(10, 20);
      display.setTextSize(1);
      display.print("Obje: ");
      display.print(temp_obj + calibration);
      display.print((char)247);
      display.print("C");

      display.setCursor(10, 30);
      display.setTextSize(1);
      display.print("BPM: ");
      display.print(temp_bpm);

      display.setCursor(10, 40);
      display.setTextSize(1);
      display.print("SpO2: ");
      display.print(temp_spo2);

      display.setCursor(10, 50);
      display.setTextSize(1);
      display.print("Hasta: ");
      display.print(temp_id_old);

      if (!digitalRead(buttonPin)) {
        display.setCursor(80, 50);
        display.print("Sifirlandi");
        temp_id_old=0;
      } else {
        display.setCursor(80, 50);
        display.print("     ");
      }
      display.display();
      LastReport_Screen = millis();
    }

    if (millis() - LastReport_temperature > 300) {
      temp_amb = mlx.readAmbientTempC();
      temp_obj = mlx.readObjectTempC();
      LastReport_temperature = millis();
    }

    vTaskDelay(1);
  }
}


void SendReadingsToFirebase(void* parameter) {
  for (;;) {
    if (millis() - sendDataPrevMillis > 3000) {
      sendDataPrevMillis = millis();
      String temp_str= "Hasta_" + String(temp_id_old) +"/";
      // Firebase.setBool(fbdo, F("/test/bool"), count % 2 == 0);
      // Firebase.setInt(fbdo, F("/test/int"), count);
      // Firebase.setFloat(fbdo, F("/test/float"), count + 10.2);
      // Firebase.setDouble(fbdo, F("/test/double"), count + 35.517549723765);
      // Firebase.setString(fbdo, F("/test/string"), "Hello World!");
      Firebase.setFloat(fbdo, temp_str + "/Ambient_Temp", temp_amb);
      Firebase.setFloat(fbdo, temp_str + "/Object_Temp", temp_obj);
      Firebase.setFloat(fbdo, temp_str + "/BPM", temp_bpm);
      Firebase.setFloat(fbdo, temp_str + "/SpO2", temp_spo2);
      Firebase.setInt(fbdo, temp_str + "/ID", temp_id_old);
    }
    vTaskDelay(1);
  }
}