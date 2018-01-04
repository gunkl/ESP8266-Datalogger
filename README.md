# ESP8266-Datalogger
Logs temp, humidity, and voltage from an Feather Huzzah using Arduino IDE to DynamoDB, with html plot example.
    On the ESP side:
    System wakes every 10 minutes from deep sleep.
    Gets NTP time. Reads the DHT for temp/humidity. Reads the ADC for voltage.  Puts the data on AWS DynamoDB.
    Lots of data gets dumped to the serial port to tell you what's going on.

# Files and Setup
**piplot.py** - Grabs data from your dynamodb (using boto3) and uses bokeh to plot graphs of your sensor data. 
            You need to know how to set up boto3 and aws credentials, as well as assign appropriate permissions
            to your dynamoDB to allow read access. It assumes default location of credentials in ~/.aws/
            You will need to edit this to change/add/remove locations, unless you too have a shed and huzzah named
            identically to mine.
            Make sure the DB configuration and locations match the ESP settings.
            
**esp8266_datalogger.ino** - Arduino project for the ESP12e-ESP8266 Feather Huzzah board.
            Hardware: Adafruit Feather Huzzah, LiPo battery, SSD1306 I2C OLED Display, DHT11 sensor
            This makes use of the AWS SDK, and you need to create keys.h and keys.cpp within the source of the project
            containing your credentials (awsKeyID, awsSecKey) in Arduino/libraries/aws-sdk-arduino-esp8266/src/keys.*
            Similarly, wificonfig.h and .cpp contain "ssid1" and "password1" for WiFi access. I placed these files in
            libraries/wificonfig/
            Make sure the location, DB table name, and settings here for putting data into the DB matches what you 
            have in piplot to pull the data.
            You may need to change the scaling in adcget() - I have it scaled for the resistor divider I used.
            You may want to update the timezone settings as well.
            
# GOTCHAS - I ran into so many of these during this project.
            NTP - I tried using a "polling interval" style NTP library.  It would take forever to get the first sync.
                  Also, the clock on the ESP is actually quite good at keeping time when not sleeping, so this would
                  have been overkill. For the purposes of deep sleep and wakeup, I switched to an instant NTP grab.
                  Unfortunately, it appears some clocks in the pool are wrong (no way) - and I've seen dates for 2036.
                  
            WiFiUDP - As soon as I added this library, the code wouldn't compile when trying to include rst_reason to
                  read why the system was reset. (Usually deep sleep)
            
            Libraries - In most cases you will need to grab the libraries and versions I've put in the code itself.
                  A lot of these are pulls from Git. Sorry to report, I don't remember which, but some of them have
                  bug fixes that I pulled from Git from specific pull requests that aren't merged yet.
                  AWS-sdk is probably one of these.

            Crashes - The AWS libraries seem to cause a lot of these, making running the code in a constant-on loop
                  not really practical.  For example, if wifi disconnects, I'm not handling it right I guess, and 
                  AWS will cause a stack crash.
            
            AWS-sdk - See above in Libraries, this one is a beast and it's under dev.  Also, the current code I'm
                  running has the SSL Cert fingerprint hard coded. I had to change it for us-west region so that
                  it would report "Certificate Matches" - see the serial output.  Side note, even if it says the cert
                  doesn't match, it still posts your data O:-)
            

# TODO
**piplot.py** - Some code cleanup
    Make it easier to add new data, ie, cleanup how dataframes are generated.
                
**esp8266_datalogger.ino** - Some code cleanup :-)
    There's some leftover code from when I was just displaying the data on the display,
    including creating a line graph, which required an array of data. This can be updated and removed
    for the current usage.
    If WiFiUDP gets fixed, or figure out how to fix it, so that I can read the wakeup/reset reason (rst_reason).
    As soon as I added WiFiUDP, this code stopped compiling.
    NTP might not actually be required at all - the AWS library gets the time and date (see x-amz-date in
    serial output) - if I bothered to figure out how to use it.
            
