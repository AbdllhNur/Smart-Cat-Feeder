#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Stepper.h>
#include <HX711_ADC.h>
#include <HX711.h>
#include "soc/rtc.h"

#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"

#define API_KEY "FIREBASE_API_KEY"
#define DATABASE_URL "FIREBASE_RTDB_DB_LINK"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;            
bool signupOK = false;

char daysOfTheWeek[7][12] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};

RTC_DS3231 rtc;

String banyak_makanan;
String makanan_tangki;

unsigned long sendDataPrevMillis = 0;

// state awal jatah harian
int jatah_harian = 5;

// variabel sensor ultrasonik
long duration;
float isi_tangki;
// pin hc-sr04
const int trigPin = 12;
const int echoPin = 13;

Stepper myStepper(2048, 32, 25, 33, 26); // IN1-IN3-IN2-IN4

// load cell pin+variable
const int HX711_dout = 15;
const int HX711_sck = 4;
unsigned long t = 0;
unsigned long stabilizingtime = 2000;
boolean _tare = true;
float isi_piring;

HX711_ADC LoadCell(HX711_dout, HX711_sck);

String tombolstr;
int status_stepper;

void firebaseSetInt(String, int);
void firebaseSetFloat(String, float);
void firebaseSetString(String, String);

void setup()
{
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("connecting to wifi.....");

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.print("connected with IP : ");
  Serial.print(WiFi.localIP());
  Serial.println();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  config.api_key = API_KEY;

  if (Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("Firebase success");
    signupOK = true;
  }
  else
  {
    String firebaseErrorMessage = config.signer.signupError.message.c_str();
    Serial.printf("%s\n", firebaseErrorMessage);
  }

  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1)
    delay(10);
  }

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  LoadCell.begin();
  float calibrationValue = 434.12;

  LoadCell.start(stabilizingtime, _tare);
  LoadCell.setCalFactor(calibrationValue); // set calibration value (float)

  myStepper.setSpeed(15);
  myStepper.step(4096);
  delay(100);

  firebaseSetInt("jatah harian makanan/tersisa", jatah_harian);
}

void loop()
{
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0))
  {
    sendDataPrevMillis = millis();
    DateTime now = rtc.now();
    // done: kodingan buat reset hari
    if (now.hour() == 23 && now.minute() == 59 && now.second() == 59)
    {
      jatah_harian = 5;
      firebaseSetInt("jatah harian makanan/tersisa", jatah_harian);
    }

    // done: kodingan real time module ngirim data ke firebase
    firebaseSetString("Date and Time/1)Hari", daysOfTheWeek[now.dayOfTheWeek()]);
    firebaseSetInt("Date and Time/2)Tanggal", now.day());
    firebaseSetInt("Date and Time/3)Bulan", now.month());
    firebaseSetInt("Date and Time/4)Tahun", now.year());
    firebaseSetInt("Date and Time/5)Jam", now.hour());
    firebaseSetInt("Date and Time/6)Menit", now.minute());
    firebaseSetInt("Date and Time/7)Detik", now.second());
    delay(1000);

    // done: kodingan sensor ultrasonik meriksa ketinggian di tangki storage
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);
    isi_tangki = duration * 0.034 / 2;

    // done: kodingan load cell
    static boolean newDataReady = 0;
    const int serialPrintInterval = 0;
    if (LoadCell.update())
      newDataReady = true;
    if (newDataReady)
    {
      if (millis() > t + serialPrintInterval)
      {
        isi_piring = LoadCell.getData();
        newDataReady = 0;
        t = millis();
        Serial.print(isi_piring);
      }
    };

    // done: cek storage makanan
    if (isi_tangki >= 7)
    {
      makanan_tangki = "habis";
    }
    else if (isi_tangki < 7 && isi_tangki > 2)
    {
      makanan_tangki = "banyak";
    }
    else
    {
      makanan_tangki = "penuh";
    }

    // done: cek piring makan
    if (isi_piring > 1)
    {
      banyak_makanan = "penuh";
    }
    else
    {
      banyak_makanan = "habis";
    }
    firebaseSetString("sisa makanan/jumlah makanan di tangki", makanan_tangki);
    firebaseSetString("sisa makanan/jumlah makanan di piring", banyak_makanan);
    // done: supply makanan harian
    if (now.hour() % 7 == 0 || now.hour() == 17)
    {
      if (now.minute() == 0)
      {
        if (jatah_harian > 0)
        {
          if (strcmp(makanan_tangki.c_str(), "penuh") == 0 || strcmp(makanan_tangki.c_str(), "banyak") == 0)
          {
            // done: kodingan penggerak stepper 45 derajat
            myStepper.setSpeed(15);
            myStepper.step(4096);
            delay(10000);
            jatah_harian -=1;
            firebaseSetString("sisa makanan/jumlah makanan di tangki", makanan_tangki);
            firebaseSetString("sisa makanan/jumlah makanan di piring", banyak_makanan);
            firebaseSetInt("jatah harian makanan/tersisa", jatah_harian);
          }
        }
      }
    }

    // done: kodingan buat gerakin motor stepper dari data firebase yg diambil dari kodular
    if (Firebase.RTDB.getInt(&fbdo, "/tombol/status"))
    {
      if (fbdo.dataType() == "string")
      {
        tombolstr = fbdo.stringData();
        status_stepper = tombolstr.toInt();
        Serial.println(status_stepper);
        if (status_stepper == 1 && jatah_harian > 0)
        {
          myStepper.setSpeed(15);
          myStepper.step(4096);
          delay(10000);
          Serial.print("dapet int");
          jatah_harian -= 1;
          status_stepper = 0;
          firebaseSetInt("tombol/status", status_stepper);
          firebaseSetString("sisa makanan/jumlah makanan di tangki", makanan_tangki);
          firebaseSetString("sisa makanan/jumlah makanan di piring", banyak_makanan);
          firebaseSetInt("jatah harian makanan/tersisa", jatah_harian);
        }
      }
    }
    else
    {
      Serial.println(fbdo.errorReason());
    }
  }
}

void firebaseSetFloat(String databaseDirectory, float value)
{
  if (Firebase.RTDB.setFloat(&fbdo, databaseDirectory, value))
  {
    Serial.print("PASSED: ");
    Serial.println(value);
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void firebaseSetString(String databaseDirectory, String value)
{
  if (Firebase.RTDB.setString(&fbdo, databaseDirectory, value))
  {
    Serial.print("PASSED: ");
    Serial.println(value);
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}

void firebaseSetInt(String databaseDirectory, int value)
{
  if (Firebase.RTDB.setInt(&fbdo, databaseDirectory, value))
  {
    Serial.print("PASSED: ");
    Serial.println(value);
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }
}
