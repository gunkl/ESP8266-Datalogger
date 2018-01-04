# ESP8266-Datalogger
Logs temp, humidity, and voltage from an ESP12e using Arduino IDE to DynamoDB, with html plot example.

# Setup
piplot.py - Grabs data from your dynamodb (using boto3) and uses bokeh to plot graphs of your sensor data. 
            You need to know how to set up boto3 and aws credentials, as well as assign appropriate permissions
            to your dynamoDB to allow read access.
            
