/*
  Copyright (c) 2018 +++ David Smith aka Gunkl +++
  This program is free software; you can redistribute it and/or modify it under the terms of the
  GNU General Public License as published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along with this program;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA 02111-1307 USA

*/

/*
   Posts the graph data to an AWS DynamoDB table using UTC epoch timestamp.
*/

/*
   Using library EasyNTPClient at version 1.1.0
   Using library Time at version 1.5
   Using library aws-sdk-arduino-esp8266 at version 0.9.1-beta
   Using library ESP8266WiFi at version 1.0
   Using library DHT at version
   Using library Wire at version
   Using library Adafruit_Sensor-master
   Using library arduino-menusystem
   ESP8266 Hardware library 2.4.0
*/
#include <FS.h> // spiffs filesystem
#include <EasyNTPClient.h> // https://github.com/aharshac/EasyNTPClient
#include <TimeLib.h>        //http://www.arduino.cc/playground/Code/Time
#include <AmazonDynamoDBClient.h>
#include <ESP8266AWSImplementations.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <SPI.h>
// #include <DHT.h>  // for DHT11/22
#include <Adafruit_Sensor.h> // for BME280
#include <Adafruit_BME280.h> // for BME280
/*
 * Change these lines in the Adafruit_BME280.cpp file if you can't get your sensor to work (ie, you didn't buy it
 * from adafruit) -- You may have a BMP280 if Humidity returns 0 after this!
    // check if sensor, i.e. the chip ID is correct
    if (read8(BME280_REGISTER_CHIPID) != 0x60)
        // return false;
 * Also use the i2c_scanner and if your BME module is 0x76 not 0x77 change this in the BME280.h file:
    #define BME280_ADDRESS                (0x76)
*/
// #include <Adafruit_BMP280.h> // for BMP280
#include <MenuSystem.h>  // https://github.com/jonblack/arduino-menusystem
#include "CustomNumericMenuItem.h"  // part of menusystem
#include "MyRenderer.h"  // part of menusystem

WiFiUDP Udp;
EasyNTPClient ntpClient(Udp, "pool.ntp.org", (0)); // 0 = GMT, use the timezone library to set the time zone.

Esp8266HttpClient httpClient;
Esp8266DateTimeProvider dateTimeProvider;

AmazonDynamoDBClient ddbClient;
ActionError actionError;
PutItemInput putItemInput;


/* Contants describing DynamoDB table and values being used. */
char AWS_REGION[32] = "us-west-1";
char AWS_ENDPOINT[32] = "amazonaws.com";
char TABLE_NAME[32] = "sensors_v3"; // table should have "location" as the partition key, and "epochtime" as the sort key.
char LOCATION[16] = "huzzah-01"; // location setting

// #define DHTTYPE DHT11
// #define DHT11_PIN 14
// #define dht_power 13 // Power DHT11 on pin 6, or separate power pin.
#define SDA 4
#define SCL 5
#define ONBOARDLED 0 // on board LED pin blink at startup
const unsigned int update_delay = 2000; // update every 2 seconds, this loop goes on until we get the time and deep sleep.
unsigned int deepsleep_time = (10 * 60 * 1000000); // 1000000 is 1 second, how long to deep sleep

// init DHT sensor - http://randomnerdtutorials.com/esp8266-dht11dht22-temperature-and-humidity-web-server-with-arduino-ide/
// DHT dht(DHT11_PIN, DHTTYPE);
Adafruit_BME280 bme; // I2C BME interface https://learn.adafruit.com/adafruit-bmp280-barometric-pressure-plus-temperature-sensor-breakout/arduino-test
// Adafruit_BMP280 bme; // I2C BMP interface https://learn.adafruit.com/adafruit-bmp280-barometric-pressure-plus-temperature-sensor-breakout/arduino-test

// ntp stuff
String currenttime = "";
String iso8601date = "";
String iso8601time = "";
time_t utc = 0;
time_t bad_time = 2085978496;
unsigned int ttl_expire_seconds = (60 * 86400); // 86400 is minutes in 60 days, * 60 = seconds for ttl expiry of data

//

// temp display and scaling stuff
int senseTempVals;
int senseHumidVals;
bool first = true;
int adcval = 0;
int batt_full_adc = 525;
int batt_empty_adc = 450;
int senseAltitudeVals;
// float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
int sensePressure;

// menu config
// forward declarations
const String format_float(const float value);
const String format_int(const float value);
const String format_color(const float value);
void on_component_selected(MenuComponent* p_menu_component);
void on_mainconfig(MenuComponent* p_menu_component);
void on_eraseconfig(MenuComponent* p_menu_component);
void on_exit(MenuComponent* p_menu_component);
void on_advancedconfig(MenuComponent* p_menu_component);

// wifi vars
char ssid1[64];
char password1[64];
char awsKeyID[24];
char awsSecKey[48];

// Menu variables

MyRenderer my_renderer;
MenuSystem ms(my_renderer);

MenuItem mm_mi1("Restart (no changes)", &on_exit);
MenuItem mm_mi2("Basic Settings", &on_mainconfig);
// MenuItem mm_mi3("Advanced Settings", &on_advancedconfig);
MenuItem mm_mi4("RESET CONFIG", &on_eraseconfig);
Menu mu1("(Menu)");
// BackMenuItem mu1_mi0("Level 2 - Back (Item)", &on_component_selected, &ms);
// MenuItem mu1_mi1("Level 2 - Item 1 (Item)", &on_component_selected);
// NumericMenuItem mu1_mi2("Level 2 - Txt Item 2 (Item)", nullptr, 0, 0, 2, 1, format_color);
// CustomNumericMenuItem mu1_mi3(12, "Level 2 - Cust Item 3 (Item)", 80, 65, 121, 3, format_int);
// NumericMenuItem mm_mi4("Level 1 - Float Item 4 (Item)", nullptr, 0.5, 0.0, 1.0, 0.1, format_float);
// NumericMenuItem mm_mi5("Level 1 - Int Item 5 (Item)", nullptr, 50, -100, 100, 1, format_int);
//
// end menu config
//

void adcget() {
  // There’s only one analog input pin, labeled ADC. To read the ADC pin, make a function call to analogRead(A0).
  // Remember that this pin has a weird maximum voltage of 1V – you’ll get a 10-bit value (0-1023) proportional to a voltage between 0 and 1V.
  // adcval = int((analogRead(A0)/float(1024))*100); // assuming a full range of the 1024 int value the adc produces
  // if 4.2v is max at 700 ADC reading, thats 4.2/700=.006V per adc tick, so 3.0/.006 = 500 making the range 500-700 of the batt (3.0V-4.2V)
  // 3.4-3.5 is probably lowest usable voltage.
  // with a 100k and 560k divider use low val of 550 (high is 700) so range is 150.
  // with 100k and 680k, use low val of 450 and high of 525 so range is 75. (3.5v-4.2v range, 450-538)
  float adcread = (analogRead(A0) - batt_empty_adc);
  if (adcread < 0) {
    adcread = 0;
  }
  adcval = int((adcread / float(batt_full_adc - batt_empty_adc)) * 100);
  if (adcval > 100) {
    adcval = 100;
  }
}

boolean fileWrite(String name, String filemode, String content) {
  char stringBuffer[3];
  //open the file for writing.
  //Modes:
  //"r"  Opens a file for reading. The file must exist.
  //"w" Creates an empty file for writing. If a file with the same name already exists, its content is erased and the file is considered as a new empty file.
  //"a" Appends to a file. Writing operations, append data at the end of the file. The file is created if it does not exist.
  //"r+"  Opens a file to update both reading and writing. The file must exist.
  //"w+"  Creates an empty file for both reading and writing.
  //"a+"  Opens a file for reading and appending.:

  //choosing w because we'll both write to the file and then read from it at the end of this function.
  sprintf(stringBuffer, "%s", (filemode).c_str());
  File file = SPIFFS.open(name.c_str(), stringBuffer);

  //verify the file opened:
  if (!file) {
    //if the file can't open, we'll display an error message;
    String errorMessage = "Can't open '" + name + "' !\r\n";
    Serial.println(errorMessage);
    return false;
  } else {
    file.write((uint8_t *)content.c_str(), content.length());
    file.close();
    return true;
  }
}


void fileRead(String name) {
  char new_int[5];
  //read file from SPIFFS and store it as a String variable
  String contents;
  File file = SPIFFS.open(name.c_str(), "r");
  if (!file) {
    String errorMessage = "Can't open '" + name + "' !\r\n";
    Serial.println(errorMessage);
  }
  else {
    if (name.equals("/datalogger.conf")) {
      Serial.println("Reading config...");
      while (file.available()) {
        //Lets read line by line from the file
        file.readStringUntil('\n').toCharArray(ssid1, 64);
        Serial.println("SSID: " + String(ssid1));
        file.readStringUntil('\n').toCharArray(password1, 64);
        // Serial.println("password: " + String(password1));
        file.readStringUntil('\n').toCharArray(AWS_REGION, 32);
        Serial.println("AWS_REGION: " + String(AWS_REGION));
        //
        file.readStringUntil('\n').toCharArray(AWS_ENDPOINT, 32);
        Serial.println("AWS_ENDPOINT: " + String(AWS_ENDPOINT));
        //
        file.readStringUntil('\n').toCharArray(TABLE_NAME, 32);
        Serial.println("TABLE_NAME: " + String(TABLE_NAME));
        //
        file.readStringUntil('\n').toCharArray(LOCATION, 16);
        Serial.println("LOCATION/hostname: " + String(LOCATION));
        //
        file.readStringUntil('\n').toCharArray(awsKeyID, 24);
        // Serial.println("awsKeyID: " + String(awsKeyID));
        //
        file.readStringUntil('\n').toCharArray(awsSecKey, 48);
        // Serial.println("awsSecKey: " + String(awsSecKey));
        //
        file.readStringUntil('\n').toCharArray(new_int, 5);
        sscanf(new_int, "%d", &batt_full_adc);
        Serial.println("ADC value when battery/voltage is full/max: " + String(batt_full_adc));
        //
        file.readStringUntil('\n').toCharArray(new_int, 5);
        sscanf(new_int, "%d", &batt_empty_adc);
        Serial.println("ADC value when battery/voltage is empty/min: " + String(batt_empty_adc));
        //
        file.readStringUntil('\n').toCharArray(new_int, 4);
        int new_deepsleep_i;
        sscanf(new_int, "%d", &new_deepsleep_i);
        Serial.println("Deep sleep (minutes): " + String(new_deepsleep_i));
        deepsleep_time = (new_deepsleep_i * 60 * 1000000);

      }
    }
  }
}


boolean fileRemove(String name) {
  //read file from SPIFFS and store it as a String variable
  SPIFFS.remove(name.c_str());
  return true;
}

void displaymenu() {
  ms.get_root_menu().add_item(&mm_mi1);
  ms.get_root_menu().add_item(&mm_mi2);
  // ms.get_root_menu().add_item(&mm_mi3);
  ms.get_root_menu().add_item(&mm_mi4);
  ms.get_root_menu().add_menu(&mu1);
  // mu1.add_item(&mu1_mi0);
  // mu1.add_item(&mu1_mi1);
  // mu1.add_item(&mu1_mi2);
  // mu1.add_item(&mu1_mi3);
  // ms.get_root_menu().add_item(&mm_mi4);
  // ms.get_root_menu().add_item(&mm_mi5);

  display_help();
  ms.display();
}

void serialFlush() {
  while (Serial.available() > 0) {
    char t = Serial.read();
  }
}

bool getmenumode() {
  char minput[5];
  Serial.setTimeout(5000);
  serialFlush();
  Serial.println("");
  Serial.println("Type mm + <enter> within 5 seconds for menu...");
  Serial.readBytesUntil('\r', minput, 5);
  if (String(minput).endsWith("mm")) {
    Serial.println("Loading menu.");
    return true;
  }
  else {
    Serial.println("Booting...");
    return false;
  }
}

void startfs() {
  SPIFFS.begin();
  // FSInfo fs_info;
  if (SPIFFS.exists("/datalogger.conf")) {
    // Serial.println("SPIFFS: " + String(SPIFFS.info(fs_info)));
    fileRead("/datalogger.conf");
  }
  else {
    Serial.println("No SPIFFS found, or no config found, formatting...");
    SPIFFS.format();
    displaymenu();
    serialFlush();
    while (true) {
      serial_handler();
    }
  }
}

void on_eraseconfig(MenuComponent* p_menu_component) {
  Serial.println("Formatting SPIFFS.");
  SPIFFS.format();
  config_reset();
}

void on_exit(MenuComponent* p_menu_component) {
  config_reset();
}

String menuinput(char *minput, String defaultval, int fieldsize) {
  // fieldsize should always be +1 the allowed size of the destination field
  int numchar = Serial.readBytesUntil('\r', minput, fieldsize);
  if (numchar > 0) {
    minput[numchar] = '\0'; // terminate string with NULL
    return minput;
  }
  else {
    Serial.println("No input provided, using prior value.");
    return defaultval;
  }
}

void on_mainconfig(MenuComponent* p_menu_component) {
  char minput[64];
  Serial.println(p_menu_component->get_name());
  Serial.setTimeout(120000);  // 120 second input timeout
  serialFlush();
  Serial.println("");
  Serial.println("Enter SSID: [" + String(ssid1) + "] ");
  String newssid1 = menuinput(minput, String(ssid1), 64);
  Serial.println("Enter password: [" + String(password1) + "] ");
  String newpassword1 = menuinput(minput, String(password1), 64);
  Serial.println("Enter AWS REGION: [" + String(AWS_REGION) + "] ");
  String new_AWS_REGION = menuinput(minput, String(AWS_REGION), 32);
  Serial.println("Enter AWS ENDPOINT: [" + String(AWS_ENDPOINT) + "] ");
  String new_AWS_ENDPOINT = menuinput(minput, String(AWS_ENDPOINT), 32);
  Serial.println("Enter DynamoDB table name: [" + String(TABLE_NAME) + "] ");
  String new_TABLE_NAME = menuinput(minput, String(TABLE_NAME), 32);
  //
  Serial.println("Enter sensor location, this is also used as hostname: [" + String(LOCATION) + "] ");
  String new_LOCATION = menuinput(minput, String(LOCATION), 16);
  //
  Serial.println("Enter DynamoDB awsKeyID: [" + String(awsKeyID) + "] ");
  String new_awsKeyID = menuinput(minput, String(awsKeyID), 24);
  Serial.println("Enter DynamoDB awsSecKey: [" + String(awsSecKey) + "] ");
  String new_awsSecKey = menuinput(minput, String(awsSecKey), 48);
  //
  Serial.println("ADC value when battery/voltage is full/max: [" + String(batt_full_adc) + "] ");
  String new_batt_full_adc = menuinput(minput, String(batt_full_adc), 5);
  Serial.println("ADC value when battery/voltage is empty/min: [" + String(batt_empty_adc) + "] ");
  String new_batt_empty_adc = menuinput(minput, String(batt_empty_adc), 5);
  // (10*60*1000000) = 10 minutes
  Serial.println("Deep sleep time in minutes (max 360): [" + String(deepsleep_time / (60 * 1000000)) + "] ");
  String new_deepsleep = menuinput(minput, String(deepsleep_time / (60 * 1000000)), 4);
  //
  Serial.println("Writing config...");
  fileWrite("/datalogger.conf", "w", newssid1 + "\n" + newpassword1 + "\n");
  fileWrite("/datalogger.conf", "a", new_AWS_REGION + "\n" + new_AWS_ENDPOINT + "\n" + new_TABLE_NAME + "\n" + new_LOCATION + "\n" + new_awsKeyID + "\n" + new_awsSecKey + "\n");
  fileWrite("/datalogger.conf", "a", new_batt_full_adc + "\n" + new_batt_empty_adc + "\n" + new_deepsleep + "\n");
  config_reset();
}


void config_reset() {
  Serial.println("Restarting to complete changes..");
  ESP.restart();
}

// make the reset info reason code work
extern "C" {
#include <user_interface.h> // https://github.com/esp8266/Arduino actually tools/sdk/include
}
//
/*
  enum rst_reason {
  REASON_DEFAULT_RST = 0,  normal startup by power on
  REASON_WDT_RST = 1,  hardware watch dog reset
  REASON_EXCEPTION_RST = 2,  exception reset, GPIO status won't change
  REASON_SOFT_WDT_RST   = 3,  software watch dog reset, GPIO status won't change
  REASON_SOFT_RESTART = 4,  software restart ,system_restart , GPIO status won't change
  REASON_DEEP_SLEEP_AWAKE = 5,  wake up from deep-sleep
  REASON_EXT_SYS_RST      = 6  external system reset
  };
*/
int checkResetReason() {
  rst_info *myResetInfo;
  delay(5000); // slow down so we really can see the reason!!
  myResetInfo = ESP.getResetInfoPtr();
  Serial.printf("myResetInfo->reason %x \n", myResetInfo->reason); // reason is uint32
  Serial.flush();
  return int(myResetInfo->reason);
}

void goDeepSleep(String displayMessage) {
  Serial.println(displayMessage);
  Serial.println("Sleeping: " + String(deepsleep_time) + " Minutes: " + String(deepsleep_time / (60 * 1000000)));
  delay(250);
  ESP.deepSleep(deepsleep_time); // 1,000,000 = 1 second
}

void setup() {
  // pinMode(dht_power, OUTPUT); // DHT11 power
  pinMode(ONBOARDLED, OUTPUT);
  digitalWrite(ONBOARDLED, HIGH); // Turn off LED
  // serial setup mode
  Serial.begin(9600);
  //
  int reset_reason = checkResetReason();
  bool ismenumode = false;
  //
  startfs(); // start up SPIFFS.
  if (reset_reason == 0 || reset_reason == 6 ) {
    ismenumode = getmenumode();
  }
  if (ismenumode) {
    displaymenu();
    serialFlush();
    while (true) {
      serial_handler();
    }
  }
  //
  //
  Wire.begin();
  bme.begin();
  // dht.begin(); // DHT11/22
  //
  // Connecting to WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid1);
  // WiFi.disconnect();
  // WiFi.persistent(false);
  // WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.setOutputPower(0);
  WiFi.hostname(String(LOCATION)); // setting the hostname fixed most of my DHCP assignment issues with my netgear router.
  WiFi.begin(ssid1, password1);
  // IPAddress ip(172, 16, 0, 23); // use DHCP by MAC anyway, also this does seem to break hostname lookups.
  // IPAddress gateway(172, 16, 0, 1);
  // IPAddress dns(8, 8, 8, 8);
  // IPAddress subnet(255,255,255,0);
  // WiFi.config(ip, dns, gateway, subnet);
  delay(1000);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    // wait for wifi to be connected
    retries += 1;
    flashled(5, 125, 125);
    // delay(1000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    digitalWrite(ONBOARDLED, LOW); // Turn on LED
    ESP.wdtFeed(); // reset watchdog timer
    /* Initialize ddbClient. */
    ddbClient.setAWSRegion(AWS_REGION);
    ddbClient.setAWSEndpoint(AWS_ENDPOINT);
    ddbClient.setAWSSecretKey(awsSecKey);
    ddbClient.setAWSKeyID(awsKeyID);
    ddbClient.setHttpClient(&httpClient);
    ddbClient.setDateTimeProvider(&dateTimeProvider);
  }
  else {
    goDeepSleep("WiFi failed to connect, giving up and sleeping.");
  }

}


void loop()
{
  float floatbuf;
  if (first) {
    first = false;
  }
  //
  int maxtries = 10; // how many times to try to get ntp time before giving up and napping.
  while ((((bad_time - 1000) < utc && utc < (bad_time + 1000)) || (int(utc) == 0)) && maxtries > 0) {
    maxtries = maxtries - 1;
    Serial.println(String(maxtries) + ") Time is not set - UTC: " + String(utc));
    utc = ntpClient.getUnixTime();
    flashled(3, 125, 125);
  }
  if (maxtries > 0) {
    Serial.println("Current time UTC: " + String(utc));
  }
  else {
    goDeepSleep("Failed to get NTP time, giving up and sleeping.");
  }
  //
  bool reading = false;
  maxtries = 10;
  while (!reading && maxtries > 0) {
    maxtries = maxtries - 1;
    // digitalWrite(dht_power, LOW); // turn on the DHT11 sensor, wired to (-) pin. (+) wired to +power bus.
    delay(150);
    /* for dht
      senseTempVals = int(dht.readTemperature(true));
      senseHumidVals = int(dht.readHumidity());
    */
    senseTempVals = int(bme.readTemperature() * 9 / 5 + 32);
    senseHumidVals = int(bme.readHumidity());
    sensePressure = int(bme.readPressure());
    // senseTempVals = int(floatbuf*9/5+32);
    // senseTempVals = int(dht.readTemperature(true));
    // senseHumidVals = int(dht.readHumidity());
    // float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
    // sensePressure = int(event.pressure);
    // senseAltitudeVals = int(bmp.pressureToAltitude(seaLevelPressure, event.pressure));
    // Serial.println("Altitude: " + String(senseAltitudeVals) + "m");
    // Serial.println("Pressure: " + String(sensePressure) + " hPa");
    //
    Serial.print("Temperature = ");
    Serial.println(senseTempVals);
    Serial.print("Humidity = ");
    Serial.println(senseHumidVals);
    Serial.print("Pressure = ");
    Serial.println(sensePressure);
    //
    if ((senseTempVals < 300 && senseHumidVals < 100) && ((int(senseTempVals) != 0 && int(senseHumidVals) != 0) || (int(sensePressure) != 0)))
    {
      reading = true;
      //digitalWrite(dht_power, HIGH); // turn off DHT11 sensor
      flashled(2, 500, 500);
      flashled(1, 250, 250);
      delay(1000);
      flashled(2, 500, 500);
      flashled(1, 250, 250);
    }
    else {
      Serial.println("Bad temp/humidity/pressure reading.");
      // digitalWrite(dht_power, HIGH); // turn off DHT sensor
      flashled(2, 500, 500);
      flashled(2, 250, 250);
      delay(1000);
      flashled(2, 500, 500);
      flashled(2, 250, 250);
      delay(1000);
    }
  }
  String sleepReason = "Failed to post to AWS. Giving up.";
  if (reading) {
    //
    adcget();
    Serial.println("ADC%: " + String(adcval));
    Serial.println("ADC: " + String(analogRead(A0)));
    delay(1); // reset watchdog timer
    putItem();
    flashled(3, 500, 500);
    flashled(1, 250, 250);
    delay(1000);
    flashled(3, 500, 500);
    flashled(1, 250, 250);
    sleepReason = "Success, got ADC reading and completed post to DB.";
  }
  else {
    sleepReason = "Failed to get a ADC reading, giving up.";
    flashled(3, 500, 500);
    flashled(2, 250, 250);
    delay(1000);
    flashled(3, 500, 500);
    flashled(2, 250, 250);
  }
  goDeepSleep(sleepReason);
  delay(update_delay);
}

void flashled(int flashes, int timeon, int timeoff) {
  while (flashes > 0) {
    digitalWrite(ONBOARDLED, LOW); // Turn on LED
    delay(timeon);
    digitalWrite(ONBOARDLED, HIGH); // Turn off LED
    delay(timeoff);
    flashes = flashes - 1;
  }
}

void putItem() {
  char numberBuffer[12];
  char stringBuffer[28];

  AttributeValue dADC;
  sprintf(numberBuffer, "%d", adcval);
  dADC.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att1("voltage", dADC);

  AttributeValue dLocation;
  sprintf(numberBuffer, "%s", (String(sensePressure)).c_str());
  dLocation.setS(LOCATION);
  MinimalKeyValuePair < MinimalString, AttributeValue > att2("location", dLocation);

  AttributeValue dType;
  dType.setS("temperature");
  MinimalKeyValuePair < MinimalString, AttributeValue > att3("sensortype", dType);

  AttributeValue dHumidity;
  sprintf(numberBuffer, "%d", senseHumidVals);
  dHumidity.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att4("humidity", dHumidity);

  AttributeValue dTemp;
  sprintf(numberBuffer, "%d", senseTempVals);
  dTemp.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att5("temperature", dTemp);

  AttributeValue dEpoch; // send ttl field as utc epoch time to enable auto data expiry on dynamodb TTL settings
  sprintf(numberBuffer, "%d", int(int(utc)));
  dEpoch.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att6("epochtime", dEpoch);

  AttributeValue dTtl; // send ttl field as utc epoch time to enable auto data expiry on dynamodb TTL settings
  sprintf(numberBuffer, "%d", int(int(utc) + ttl_expire_seconds));
  dTtl.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att7("ttl", dTtl);

  AttributeValue dPressure;
  sprintf(numberBuffer, "%d", int(sensePressure));
  dPressure.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att8("pressure", dPressure);

  MinimalKeyValuePair<MinimalString, AttributeValue> itemArray[] = { att1, att2, att3, att4, att5, att6, att7, att8 };

  /* Set values for putItemInput. */
  putItemInput.setItem(MinimalMap < AttributeValue > (itemArray, 8));
  putItemInput.setTableName(TABLE_NAME);

  /* perform putItem and check for errors. */
  PutItemOutput putItemOutput = ddbClient.putItem(putItemInput, actionError);
  switch (actionError) {
    case NONE_ACTIONERROR:
      Serial.println("PutItem succeeded!");
      break;
    case INVALID_REQUEST_ACTIONERROR:
      Serial.print("ERROR: ");
      Serial.println(putItemOutput.getErrorMessage().getCStr());
      break;
    case MISSING_REQUIRED_ARGS_ACTIONERROR:
      Serial.println("ERROR: Required arguments were not set for PutItemInput");
      break;
    case RESPONSE_PARSING_ACTIONERROR:
      Serial.println("ERROR: Problem parsing http response of PutItem");
      break;
    case CONNECTION_ACTIONERROR:
      Serial.println("ERROR: Connection problem");
      break;
  }
}

String zeropad(int val)
{
  String zeroval = "";
  if (val < 10) {
    zeroval = "0" + String(val);
  }
  else {
    zeroval = String(val);
  }
  return zeroval;
}

// Menu callback function

// writes the (int) value of a float into a char buffer.
const String format_int(const float value) {
  return String((int) value);
}

// writes the value of a float into a char buffer.
const String format_float(const float value) {
  return String(value);
}

// writes the value of a float into a char buffer as predefined colors.
const String format_color(const float value) {
  String buffer;

  switch ((int) value)
  {
    case 0:
      buffer += "Red";
      break;
    case 1:
      buffer += "Green";
      break;
    case 2:
      buffer += "Blue";
      break;
    default:
      buffer += "undef";
  }

  return buffer;
}

// In this example all menu items use the same callback.

void on_component_selected(MenuComponent* p_menu_component) {
  Serial.println(p_menu_component->get_name());
}


void display_help() {
  Serial.println("");
  Serial.println("***************");
  Serial.println("w: go to previus item (up)");
  Serial.println("s: go to next item (down)");
  Serial.println("a: go back (right)");
  Serial.println("d: select \"selected\" item");
  Serial.println("?: print this help");
  Serial.println("h: print this help");
  Serial.println("***************");
}

void serial_handler() {
  char inChar[2];
  Serial.setTimeout(120000);
  Serial.readBytesUntil('\r', inChar, 2);
  // Serial.println("\033c");
  switch (inChar[0]) {
    case 'w': // Previous item
      ms.prev();
      ms.display();
      Serial.println("");
      break;
    case 's': // Next item
      ms.next();
      ms.display();
      Serial.println("");
      break;
    case 'a': // Back presed
      ms.back();
      ms.display();
      Serial.println("");
      break;
    case 'd': // Select presed
      ms.select();
      ms.display();
      Serial.println("");
      break;
    case '?':
    case 'h': // Display help
      ms.display();
      Serial.println("");
      break;
    default:
      break;
  }
}

