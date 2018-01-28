# ESP8266-Datalogger (BME280 version)
Logs temp, humidity, and voltage from an Feather Huzzah using Arduino IDE to DynamoDB, with html plot example.
 * On the ESP side:
   * System wakes every 10 minutes from deep sleep.
   * Gets NTP time. Reads the BME280 for temp/humidity/pressure. Reads the ADC for voltage.  Puts the data on AWS DynamoDB.
   * Lots of data gets dumped to the serial port to tell you what's going on.
   * You can enable dynamodb "TTL" to expire data. Default is to post a TTL of +60 days.
   * Serial port configuration is possible for many settings at board startup.

# LED Operation - blink decoder
 * 5 quick/constant fast blink - waiting for WiFi
 * 3 quick - waiting for NTP/time
 * 2 slow 1 fast (x2) - sensor read success
 * 2 slow 2 fast (x2) - sensor read failure
 * 3 slow 1 fast (x2) - success posting to AWS
 * 3 slow 2 fast (x2) - failure to post, giving up and going to sleep.

# Files and Setup
**piplot.py** - Grabs data from your dynamodb (using boto3) and uses bokeh to plot graphs of your sensor data. 
 * You need to know how to set up boto3 and aws credentials, as well as assign appropriate permissions
            to your dynamoDB to allow read access. It assumes default location of credentials in ~/.aws/
 * You will need to edit this to change/add/remove locations, unless you too have a shed and huzzah named
            identically to mine.
 * Make sure the DB configuration and locations match the ESP settings.
            
**esp8266_datalogger.ino** - Arduino project for the ESP12e-ESP8266 Feather Huzzah board.
 * Hardware: Adafruit Feather Huzzah, LiPo battery, BME280 sensor wired for i2c
 * This makes use of the AWS SDK, and you need to create keys.h and keys.cpp within the source of the project
            containing your credentials (awsKeyID, awsSecKey) in Arduino/libraries/aws-sdk-arduino-esp8266/src/keys.*
 * Make sure the location, DB table name, and settings here for putting data into the DB matches what you 
            have in piplot to pull the data.
 * You may need to change the scaling in adcget() - I have it scaled for the resistor divider I used.
            
# GOTCHAS
            
 * Libraries - In most cases you will need to grab the libraries and versions I've put in the code itself.
    * A lot of these are pulls from Git. Sorry to report, I don't remember which, but some of them have
                  bug fixes that I pulled from Git from specific pull requests that aren't merged yet.
                  AWS-sdk is probably one of these.

 * Crashes - The AWS libraries seem to cause a lot of these, making running the code in a constant-on loop
                  not really practical.  For example, if wifi disconnects, I'm not handling it right I guess, and 
                  AWS will cause a stack crash.
            
 * AWS-sdk - See above in Libraries, this one is a beast and it's under dev.  Also, the current code I'm
                  running has the SSL Cert fingerprint hard coded. I had to change it for us-west region so that
                  it would report "Certificate Matches" - see the serial output.  Side note, even if it says the cert
                  doesn't match, it still posts your data O:-)
            

# TODO
**piplot.py** - Some code cleanup
 * Make it easier to add new data, ie, cleanup how dataframes are generated.
                
**esp8266_datalogger.ino** - Some code cleanup :-)
 * NTP might not actually be required at all - the AWS library gets the time and date (see x-amz-date in
    serial output) - if I bothered to figure out how to use it.
            
