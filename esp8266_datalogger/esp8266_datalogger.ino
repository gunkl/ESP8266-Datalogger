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

/* Displays temp, humidity, and voltage on the OLED SSD1306 display.
   Posts the graph data to an AWS DynamoDB table in your timezone corrected time, as ISO8601 time.
*/

/*
   Using library EasyNTPClient at version 1.1.0
   Using library Time at version 1.5
   Using library Timezone
   Using library aws-sdk-arduino-esp8266 at version 0.9.1-beta (also contains keys.h/cpp with aws keys)
   Using library ESP8266WiFi at version 1.0
   Using library wificonfig (contains wifi ssid and password)
   Using library DHT at version
   Using library Wire at version
   Using library esp8266-oled-ssd1306
   Using library Adafruit_Sensor-master
*/
#include <FS.h> // spiffs filesystem
#include <EasyNTPClient.h> // https://github.com/aharshac/EasyNTPClient
#include <TimeLib.h>        //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>    //https://github.com/JChristensen/Timezone
#include <AmazonDynamoDBClient.h>
#include <ESP8266AWSImplementations.h>
#include <keys.h>

// #include <wificonfig.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <Wire.h>
#include <SSD1306.h>
#include <MenuSystem.h>
#include "CustomNumericMenuItem.h"
#include "MyRenderer.h"

WiFiUDP Udp;
EasyNTPClient ntpClient(Udp, "pool.ntp.org", (0)); // 0 = GMT, use the timezone library to set the time zone.

Esp8266HttpClient httpClient;
Esp8266DateTimeProvider dateTimeProvider;

AmazonDynamoDBClient ddbClient;
ActionError actionError;
PutItemInput putItemInput;


/* Contants describing DynamoDB table and values being used. */
static const char* AWS_REGION = "us-west-1";
static const char* AWS_ENDPOINT = "amazonaws.com";
const char* TABLE_NAME = "sensors_v2"; // table should have "location" as the partition key, and "time_utc_iso8601" as the sort key.

#define DHTTYPE DHT11
#define DHT11_PIN 14
#define dht_power 13 // Power DHT11 on pin 6, or separate power pin.
#define SDA 4
#define SCL 5
#define SSD1306_ADDRESS 0x3C
#define ONBOARDLED 0 // on board LED pin blink at startup
const unsigned int update_delay = 2000; // update every 2 seconds, this loop goes on until we get the time and deep sleep.
const unsigned int deepsleep_time = (10*60*1000000); // 1000000 is 1 second, how long to deep sleep

// init DHT sensor - http://randomnerdtutorials.com/esp8266-dht11dht22-temperature-and-humidity-web-server-with-arduino-ide/
DHT dht(DHT11_PIN, DHTTYPE);
// init display - https://github.com/squix78/esp8266-oled-ssd1306
SSD1306 display(SSD1306_ADDRESS, SDA, SCL);

// ntp stuff
String currenttime = "";
String iso8601date = "";
String iso8601time = "";
bool timeisset = false;
//US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {"PDT", Second, Sun, Mar, 2, -420};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"PST", First, Sun, Nov, 2, -480};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;
int8_t timeZone = 0;

//

// temp display and scaling stuff
const int SENSE_ARRAY_SIZE = 128;
int senseTempVals[SENSE_ARRAY_SIZE];
int scaleTempMinDefault = 50;
int scaleTempMin = 50;
int scaleTempMaxDefault = 80;
int scaleTempMax = 80;
int scaleTempVar = 20; // add/sub 10 degrees (20 total) to min/max for scale.
int senseHumidVals[SENSE_ARRAY_SIZE];
int scaleHumidMinDefault = 25;
int scaleHumidMin = 25;
int scaleHumidMax = 60;
int scaleHumidMaxDefault = 60;
int scaleHumidVar = 20;
bool first = true;
int adcval = 0;

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

// Menu variables

MyRenderer my_renderer;
MenuSystem ms(my_renderer);

MenuItem mm_mi1("Restart (no changes)", &on_exit);
MenuItem mm_mi2("Set Wireless SSID / Password", &on_mainconfig);
MenuItem mm_mi3("Advanced Settings", &on_advancedconfig);
MenuItem mm_mi4("RESET CONFIG", &on_eraseconfig);
Menu mu1("Level 1 - Item 3 (Menu)");
// BackMenuItem mu1_mi0("Level 2 - Back (Item)", &on_component_selected, &ms);
// MenuItem mu1_mi1("Level 2 - Item 1 (Item)", &on_component_selected);
// NumericMenuItem mu1_mi2("Level 2 - Txt Item 2 (Item)", nullptr, 0, 0, 2, 1, format_color);
// CustomNumericMenuItem mu1_mi3(12, "Level 2 - Cust Item 3 (Item)", 80, 65, 121, 3, format_int);
// NumericMenuItem mm_mi4("Level 1 - Float Item 4 (Item)", nullptr, 0.5, 0.0, 1.0, 0.1, format_float);
// NumericMenuItem mm_mi5("Level 1 - Int Item 5 (Item)", nullptr, 50, -100, 100, 1, format_int);
//
// end menu config
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

// Manage network connection
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo) {
  Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
  digitalWrite(ONBOARDLED, LOW); // Turn on LED
}

// Manage network disconnection
void onSTADisconnected(WiFiEventStationModeDisconnected event_info) {
  Serial.printf("Disconnected from SSID: %s\n", event_info.ssid.c_str());
  Serial.printf("Reason: %d\n", event_info.reason);
  digitalWrite(ONBOARDLED, HIGH); // Turn off LED
}

void adcget() {
  // There’s only one analog input pin, labeled ADC. To read the ADC pin, make a function call to analogRead(A0). 
  // Remember that this pin has a weird maximum voltage of 1V – you’ll get a 10-bit value (0-1023) proportional to a voltage between 0 and 1V.
  // adcval = int((analogRead(A0)/float(1024))*100); // assuming a full range of the 1024 int value the adc produces
  adcval = int((analogRead(A0)/float(700))*100); // my huzzah with a divider using 560k and 100k resistors shows ~700 to be 100%
  if (adcval > 100){
      adcval = 100;
  }
}

boolean fileWrite(String name, String content){
  //open the file for writing.
  //Modes:
  //"r"  Opens a file for reading. The file must exist.
  //"w" Creates an empty file for writing. If a file with the same name already exists, its content is erased and the file is considered as a new empty file.
  //"a" Appends to a file. Writing operations, append data at the end of the file. The file is created if it does not exist.
  //"r+"  Opens a file to update both reading and writing. The file must exist.
  //"w+"  Creates an empty file for both reading and writing.
  //"a+"  Opens a file for reading and appending.:

  //choosing w because we'll both write to the file and then read from it at the end of this function.
  File file = SPIFFS.open(name.c_str(), "w");

  //verify the file opened:
  if (!file) {
    //if the file can't open, we'll display an error message;
    String errorMessage = "Can't open '" + name + "' !\r\n";
    Serial.println(errorMessage);
    return false;
  } else{
    //To all you old school C++ programmers this probably makes perfect sense, but to the rest of us mere mortals, here is what is going on:
    //The file.write() method has two arguments, buffer and length. Since this example is writing a text string, we need to cast the
    //text string (it's named "content") into a uint8_t pointer. If pointers confuse you, you're not alone! 
    //I don't want to go into detail about pointers here, I'll do another example with casting and pointers, for now, this is the syntax
    //to write a String into a text file.
    file.write((uint8_t *)content.c_str(), content.length());
    file.close();
    return true;
  }
}


void fileRead(String name){
  //read file from SPIFFS and store it as a String variable
  String contents;
  File file = SPIFFS.open(name.c_str(), "r");
  if (!file) {
    String errorMessage = "Can't open '" + name + "' !\r\n";
    Serial.println(errorMessage);
  }
  else {
    if (name.equals("/datalogger.conf")){
      Serial.println("Reading config...");
      while(file.available()) {
        //Lets read line by line from the file
        file.readStringUntil('\n').toCharArray(ssid1, 64);
        // Serial.println("SSID: " + String(ssid1));
        file.readStringUntil('\n').toCharArray(password1, 64);
        // Serial.println("password: " + String(password1));
    }
   }
  }
}


boolean fileRemove(String name){
  //read file from SPIFFS and store it as a String variable
  SPIFFS.remove(name.c_str());
  return true;
}

void displaymenu() {
    ms.get_root_menu().add_item(&mm_mi1);
    ms.get_root_menu().add_item(&mm_mi2);
    ms.get_root_menu().add_item(&mm_mi3);
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

void serialFlush(){
  while(Serial.available() > 0) {
    char t = Serial.read();
  }
} 

bool getmenumode(){
  char minput[5];
  Serial.setTimeout(5000);
  serialFlush();
  Serial.println("");
  Serial.println("Type mm + <enter> within 5 seconds for menu...");
  Serial.readBytesUntil('\r', minput, 5);
  if (String(minput).endsWith("mm")){
    Serial.println("Loading menu.");
    return true;
  }
  else {
    Serial.println("mm not detected, got: " + String(minput));
    return false;
  }
}

void startfs() {
  SPIFFS.begin();
  // FSInfo fs_info;
  if (SPIFFS.exists("/datalogger.conf")){
    // Serial.println("SPIFFS: " + String(SPIFFS.info(fs_info)));
    fileRead("/datalogger.conf");
  }
  else {
    Serial.println("No SPIFFS found, formatting...");
    SPIFFS.format();
    displaymenu();
    serialFlush();
    while (true){
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

String menuinput(char *minput, String defaultval){
    int numchar = Serial.readBytesUntil('\r', minput, 64);
    if (numchar > 0){
      minput[numchar]='\0';  // terminate string with NULL
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
    String newssid1 = menuinput(minput, String(ssid1));
    Serial.println("Enter password: [" + String(password1) + "] ");
    String newpassword1 = menuinput(minput, String(password1));
    Serial.println("Writing config...");
    fileWrite("/datalogger.conf", newssid1 + "\n" + newpassword1 + "\n");
    config_reset();
}

void on_advancedconfig(MenuComponent* p_menu_component) {
    char minput[64];
    Serial.println(p_menu_component->get_name());
    Serial.setTimeout(120000);  // 120 second input timeout
    serialFlush();
    Serial.println("");
    Serial.println("Enter SSID: [" + String(ssid1) + "] ");
    String newssid1 = menuinput(minput, String(ssid1));
    Serial.println("Enter password: [" + String(password1) + "] ");
    String newpassword1 = menuinput(minput, String(password1));
    Serial.println("Writing config...");
    fileWrite("/datalogger.conf", newssid1 + "\n" + newpassword1 + "\n");
    config_reset();
}

void config_reset(){
    Serial.println("Restarting to complete changes..");
    ESP.reset();
}

void setup() {
  // serial setup mode
  Serial.begin(9600);
  startfs(); // start up SPIFFS.
  bool ismenumode = getmenumode();
  if (ismenumode) {
    displaymenu();
    serialFlush();
    while (true){
      serial_handler();
    }
  }
  //Serial.println(String("ResetInfo.reason = ") + rsti->reason);
  //
  static WiFiEventHandler e1, e2;
  e1 = WiFi.onStationModeGotIP(onSTAGotIP);// As soon WiFi is connected, start NTP Client
  e2 = WiFi.onStationModeDisconnected(onSTADisconnected);
  //
  // as soon as i add the udp module, this rsti stuff vomits and dont work. no compily.
  // otherwise it's nice to know why we woke up from deep sleep.
  //rst_info *rsti;
  //rsti = ESP.getResetInfoPtr();
  //
  pinMode(dht_power, OUTPUT);
  dht.begin();
  //
  Wire.begin();
  //
  // SSD OLED 1306 setup
  display.init();
  display.flipScreenVertically();
  display.clear();   // clears the screen and buffer
  display.setFont(ArialMT_Plain_16);
  //
  //
  // Connecting to WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid1);

  WiFi.disconnect();
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.setOutputPower(0);
  WiFi.hostname("huzzah-01"); // setting the hostname fixed most of my DHCP assignment issues with my netgear router.
  WiFi.begin(ssid1, password1);
  // IPAddress ip(172, 16, 0, 23); // use DHCP by MAC anyway, also this does seem to break hostname lookups.
  // IPAddress gateway(172, 16, 0, 1);
  // IPAddress dns(8, 8, 8, 8);
  // IPAddress subnet(255,255,255,0);
  // WiFi.config(ip, dns, gateway, subnet);
  delay(1000);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    retries += 1;
    delay(1000);
    Serial.print(".");
    if (retries >= 10) {
      retries = 0;
      Serial.println("Reset WiFi..");
      WiFi.disconnect();
      delay(250);
      WiFi.mode(WIFI_OFF);
      WiFi.mode(WIFI_STA);
      WiFi.setOutputPower(0);
      WiFi.hostname("huzzah-01");
      WiFi.begin(ssid1, password1);
      delay(250);
    }
  }
  Serial.println("");
  Serial.println("WiFi connected");
  ESP.wdtFeed(); // reset watchdog timer
  /* Initialize ddbClient. */
  ddbClient.setAWSRegion(AWS_REGION);
  ddbClient.setAWSEndpoint(AWS_ENDPOINT);
  ddbClient.setAWSSecretKey(awsSecKey);
  ddbClient.setAWSKeyID(awsKeyID);
  ddbClient.setHttpClient(&httpClient);
  ddbClient.setDateTimeProvider(&dateTimeProvider);
}

void loop()
{
  if (first) {
    first = false;
    memset(senseTempVals, 0, SENSE_ARRAY_SIZE);  // FIXME: I dont need these arrays 
    memset(senseHumidVals, 0, SENSE_ARRAY_SIZE); // now that i only submit one value per deep sleep cycle.
    timeisset = false;
  }
  //
  utc = ntpClient.getUnixTime();
  local = myTZ.toLocal(utc, &tcr);
  tz8601time(local, tcr -> abbrev);
  Serial.println("Current time: " + iso8601date + iso8601time);
  if (!iso8601date.startsWith("1970")) {
    Serial.println("Time is set");
    timeisset = true;
  }
  else {
    Serial.println("Time is not set");
    timeisset = false;
  }
  //
  bool reading = false;
  while (!reading) {
    digitalWrite(dht_power, LOW); // turn on the DHT sensor, wired to (-) pin. (+) wired to +power bus.
    delay(150);
    senseTempVals[SENSE_ARRAY_SIZE - 1] = int(dht.readTemperature(true));
    senseHumidVals[SENSE_ARRAY_SIZE - 1] = int(dht.readHumidity());
    if (senseTempVals[SENSE_ARRAY_SIZE - 1] < 300)
    {
      reading = true;
      digitalWrite(dht_power, HIGH); // turn off DHT sensor
    }
    else {
      Serial.println("Bad DHT reading.");
      digitalWrite(dht_power, HIGH); // turn off DHT sensor
      delay(1000);
    }
  }
  //
  Serial.print("Temperature = ");
  Serial.println(senseTempVals[SENSE_ARRAY_SIZE - 1]);
  Serial.print("Humidity = ");
  Serial.println(senseHumidVals[SENSE_ARRAY_SIZE - 1]);
  //
  ESP.wdtFeed(); // reset watchdog timer

  if (reading) {
    if (timeisset) {
      //
      adcget();
      Serial.println("ADC%: " + String(adcval));
      Serial.println("ADC: " + String(analogRead(A0)));
      // rotatevals(); // removing because we dont need to display this anymore, the line graph isnt helpful anyway ;)
      printtext((String(senseTempVals[SENSE_ARRAY_SIZE - 1]) + (char)247 + "F" + " - " + iso8601time.substring(0, 8)), String(senseHumidVals[SENSE_ARRAY_SIZE - 1]) + "% Humid", "Batt: " + String(adcval) + "%", "");
      // rescale(); // dont need to rescale either (see rotatevals)
      ESP.wdtFeed(); // reset watchdog timer
      putItem();
      Serial.println("Sleeping: " + String(deepsleep_time));
      delay(250);
      display.displayOff();
      ESP.deepSleep(deepsleep_time); // 1,000,000 = 1 second
    }
  }
  delay(update_delay);
}

void putItem() {
  char numberBuffer[4];
  char stringBuffer[65];

  AttributeValue dTime;
  sprintf(stringBuffer, "%s", (iso8601date + iso8601time).c_str());
  dTime.setS(stringBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att6("time_utc_iso8601", dTime);

  AttributeValue dLocation;
  dLocation.setS("huzzah-01");
  MinimalKeyValuePair < MinimalString, AttributeValue > att2("location", dLocation);

  AttributeValue dType;
  dType.setS("temperature");
  MinimalKeyValuePair < MinimalString, AttributeValue > att3("sensortype", dType);

  AttributeValue dHumidity;
  sprintf(numberBuffer, "%d", senseHumidVals[SENSE_ARRAY_SIZE - 1]);
  dHumidity.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att4("humidity", dHumidity);

  AttributeValue dTemp;
  sprintf(numberBuffer, "%d", senseTempVals[SENSE_ARRAY_SIZE - 1]);
  dTemp.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att5("temperature", dTemp);

  AttributeValue dADC;
  sprintf(numberBuffer, "%d", adcval);
  dADC.setN(numberBuffer);
  MinimalKeyValuePair < MinimalString, AttributeValue > att1("voltage", dADC);

  MinimalKeyValuePair<MinimalString, AttributeValue> itemArray[] = { att1, att2, att3, att4, att5, att6 };

  /* Set values for putItemInput. */
  putItemInput.setItem(MinimalMap < AttributeValue > (itemArray, 6));
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

/* - this code draws a line graph on the display, but since we're sleeping, i dont really want to use it.
 void drawgraph(uint16_t startx, uint16_t starty, uint16_t heighty, uint16_t scalemin, uint16_t scalemax, int dispVals[SENSE_ARRAY_SIZE]) {
  // take diff between scalemax and scalemin and divide by total pixels available per unit in heighty
  int perPixel = int((scalemax - scalemin) / heighty);
  int yHeight = 0;
  for (uint16_t i = 0; i < SENSE_ARRAY_SIZE; i++) {
    // Serial.println(String(i) + "Sense: " + String(dispVals[i]));
    if (int(dispVals[i] - scalemin) <= 0)
    {
      yHeight = 0;
    }
    else if (dispVals[i] > scalemax)
    {
      yHeight = heighty;
    }
    else
    {
      yHeight = int((dispVals[i] - scalemin) / perPixel);
      if (yHeight > heighty) {
        yHeight = heighty;
      }
    }
    // Serial.println("Height " + String(yHeight) + " StartY: " + String(starty) + " EndY: " + String (starty - yHeight));
    // drawing a line as below produces a "bar" ugly chart.
    display.setPixel(i, starty - yHeight);
  }
}
*/ 

void rotatevals() {
  for (uint8_t i = 0; i < SENSE_ARRAY_SIZE - 1; i++) {
    senseTempVals[i] = senseTempVals[i + 1];
    senseHumidVals[i] = senseHumidVals[i + 1];
  }
}

void printtext(String line1, String line2, String line3, String line4) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, line1);
  display.drawString(0, 16, line2);
  display.drawString(0, 32, line3);
  display.drawString(0, 48, line4);
  // removing drawgraphs to save power, since this isn't constantly running code.
  // drawgraph(0, 31, 14, scaleTempMin, scaleTempMax, senseTempVals);
  // drawgraph(0, 63, 14, scaleHumidMin, scaleHumidMax, senseHumidVals);
  display.display();
}

/* This is part of the line graph code, which we aren't using.
void rescale() {
  int maxfoundT = scaleTempMaxDefault;
  int minfoundT = scaleTempMinDefault;
  int maxfoundH = scaleHumidMaxDefault;
  int minfoundH = scaleHumidMinDefault;
  for (uint16_t i = 0; i < SENSE_ARRAY_SIZE; i++) {
    if (!(int(senseTempVals[i]) == 0) && !(int(senseHumidVals[i]) == 0)) {
      // Serial.println(String(i) + String(minfoundH) + " " + String(minfoundT));
      if (int(senseTempVals[i]) > int(maxfoundT)) {
        maxfoundT = senseTempVals[i];
      }
      if (int(senseTempVals[i]) <= int(minfoundT)) {
        minfoundT = senseTempVals[i];
      }
      if (int(senseHumidVals[i]) > int(maxfoundH)) {
        maxfoundH = senseHumidVals[i];
      }
      if (int(senseHumidVals[i]) <= int(minfoundH)) {
        minfoundH = senseHumidVals[i];
      }
    }
  }
  // Serial.println(String(maxfoundT) + " " + String(minfoundT));
  //Serial.println(String(maxfoundH) + " " + String(minfoundH));
  //
  // humidity minmax
  scaleHumidMin = int(minfoundH - scaleHumidVar / 4);
  scaleHumidMax = int(maxfoundH + scaleHumidVar / 4);
  // temp minmax
  scaleTempMin = int(minfoundT - scaleTempVar / 4);
  scaleTempMax = int(maxfoundT + scaleTempVar / 4);
  //
  if (scaleTempMin < 0) {
    scaleTempMin = 0;
  }
  if (scaleHumidMin < 0) {
    scaleHumidMin = 0;
  }
  Serial.println("HMi:" + String(scaleHumidMin) + " HMx:" + String(scaleHumidMax) + "  TMi:" + String(scaleTempMin) + " TMx:" + String(scaleTempMax));
}
*/

void tz8601time(time_t t, char *tz) {
  iso8601date = String(year(t)) + "-" + zeropad(month(t)) + "-" + zeropad(day(t)) + "T";
  iso8601time = zeropad(hour(t)) + ":" + zeropad(minute(t)) + ":" + zeropad(second(t)) + ".000000";
}

//Function to print time with time zone
void printTime(time_t t, char *tz)
{
  sPrintI00(hour(t));
  sPrintDigits(minute(t));
  sPrintDigits(second(t));
  Serial.print(' ');
  Serial.print(dayShortStr(weekday(t)));
  Serial.print(' ');
  sPrintI00(day(t));
  Serial.print(' ');
  Serial.print(monthShortStr(month(t)));
  Serial.print(' ');
  Serial.print(year(t));
  Serial.print(' ');
  Serial.print(tz);
  Serial.println();
}

//Print an integer in "00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void sPrintI00(int val)
{
  if (val < 10) Serial.print('0');
  Serial.print(val, DEC);
  return;
}

//Print an integer in ":00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void sPrintDigits(int val)
{
  Serial.print(':');
  if (val < 10) Serial.print('0');
  Serial.print(val, DEC);
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

    switch((int) value)
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
