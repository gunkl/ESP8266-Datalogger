#!/usr/bin/python
# -*- coding: UTF-8 -*-
#
#  Copyright (c) 2015-2018 +++ David Smith - Smith Enterprises +++
#  This program is free software; you can redistribute it and/or modify it under the terms of the
#  GNU General Public License as published by the Free Software Foundation; either version 2 of
#  the License, or (at your option) any later version.
#  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
#  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the GNU General Public License for more details.
#  You should have received a copy of the GNU General Public License along with this program;
#  if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
#  Boston, MA 02111-1307 USA
#
#
import sys
reload(sys)
sys.setdefaultencoding('utf8')
import traceback
import json
import time
import datetime
import boto3
import decimal
import dateutil.parser

import pandas as pd
from bokeh.plotting import figure
from bokeh.io import output_file, save
from bokeh.layouts import column
from boto3.dynamodb.conditions import Key, Attr

dynamodb_table = "sensors_v3"  # Send updates to AWS DynamoDB, or set to None to not do this.
locations = ['huzzah-01', 'shed-outside']

class DecimalEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, decimal.Decimal):
            return str(o)
        return super(DecimalEncoder, self).default(o)


def get_data(locations=None):
    dynamodb = boto3.resource('dynamodb')
    table = dynamodb.Table(dynamodb_table)
    # pdata = []
    ddata = {}
    for location in locations:
        ddata[location] = {}
        ddata[location]['humidity'] = []
        ddata[location]['temperature'] = []
        ddata[location]['pressure'] = []
        ddata[location]['pressureHg'] = []
        ddata[location]['time'] = []
        ddata[location]['voltage'] = []
        rawdata = query_dynamo(table=table, location=location)
        for item in rawdata:
            # pdata.append({'location':location, 'humidity':int(item.get('humidity')), 'temperature':int(item.get('temperature')), 'time':int(time.mktime((dateutil.parser.parse(item.get('epochtime'))).timetuple()))})
            ddata[location]['humidity'].append(int(item.get('humidity', 0)))
            ddata[location]['temperature'].append(int(item.get('temperature', 0)))
            ddata[location]['pressure'].append(int(item.get('pressure', 0)))
            ddata[location]['pressureHg'].append(float(item.get('pressure', 0.0))*0.0002953)
            ddata[location]['voltage'].append(int(item.get('voltage', 0)))
            # ddata[location]['time'].append(dateutil.parser.parse(item.get('time_utc_iso8601')))
            ddata[location]['time'].append(datetime.datetime.fromtimestamp((item.get('epochtime'))))
            # ddata[location]['time'].append((item.get('epochtime')))
    return ddata


def query_dynamo(table=None, location=None):
    # startdate = str((datetime.datetime.utcnow() - datetime.timedelta(days=7)).isoformat())
    # enddate = str(datetime.datetime.utcnow().isoformat())
    startdate = str((datetime.datetime.utcnow() - datetime.timedelta(days=7)))
    enddate = str(datetime.datetime.utcnow())
    response = table.query(
        ProjectionExpression="humidity, temperature, pressure, setpoint, epochtime, voltage",
        # ExpressionAttributeNames={"#yr": "year"},  # Expression Attribute Names for Projection Expression only.
        # KeyConditionExpression=Key('year').eq(1992) & Key('title').between('A', 'L')
        # KeyConditionExpression=Key('location').eq(location) & Key('time').between((datetime.datetime.utcnow() - datetime.timedelta(days=7)).isoformat(),datetime.datetime.utcnow().isoformat()),
        KeyConditionExpression=Key('location').eq(location) & Key('epochtime').between(int(time.time() - 86400*7),int(time.time())),
        # ExpressionAttributeValues= {
        #    ":start_date": str((datetime.datetime.utcnow() - datetime.timedelta(days=7)).isoformat()),
        #    ":end_date": str(datetime.datetime.utcnow().isoformat())},
        # FilterExpression="time_utc_iso8601 between :start_date and :end_date",
    )
    results = response.get(u'Items')
    while 'LastEvaluatedKey' in response:
        response = table.query(
            ProjectionExpression="humidity, temperature, pressure, setpoint, epochtime, voltage",
            KeyConditionExpression=Key('location').eq(location) & Key('epochtime').between(int(time.time() - 86400*7),int(time.time())),
            ExclusiveStartKey=response['LastEvaluatedKey']
        )
        results += response.get(u'Items')
    return results


def main_run():
    # pdata, ddata = get_data(locations=locations)
    ddata = get_data(locations=locations)
    """
    dbitem['pkey'] = str(uuid.uuid4()) - no longer needed.
    dbitem['temperature'] = int(tempF)
    dbitem['setpoint'] = int(fridgemode)
    dbitem['voltage'] = int(voltage)
    dbitem['humidity'] = int(humidity)
    dbitem['pressure'] = int(pressure)
    dbitem['no_actuate'] = no_actuate
    dbitem['time'] = int(datetime.datetime.utcnow())
    """

    df1 = pd.DataFrame(
        ddata['shed-outside']['temperature'],
        index=ddata['shed-outside']['time'],
        columns=['Shed Temp']
    )
    df2 = pd.DataFrame(
        ddata['huzzah-01']['temperature'],
        index=ddata['huzzah-01']['time'],
        columns=['Huzzah-01 Temp']
    )

    df3 = pd.DataFrame(
        ddata['shed-outside']['humidity'],
        index=ddata['shed-outside']['time'],
        columns=['Shed Humidity']
    )
    df4 = pd.DataFrame(
        ddata['huzzah-01']['humidity'],
        index=ddata['huzzah-01']['time'],
        columns=['Huzzah-01 Humidity']
    )
    df5 = pd.DataFrame(
        ddata['huzzah-01']['voltage'],
        index=ddata['huzzah-01']['time'],
        columns=['Huzzah-01 Voltage']
    )
    df6 = pd.DataFrame(
        ddata['huzzah-01']['pressureHg'],
        index=ddata['huzzah-01']['time'],
        columns=['Huzzah-01 Pressure (Hg)']
    )

    s1 = figure(title='Temperature', plot_width=800, plot_height=500, x_axis_type='datetime')
    s1.line(x=df1.index, y=df1['Shed Temp'], color="navy", legend="Shed")
    s1.line(x=df2.index, y=df2['Huzzah-01 Temp'], color="green", legend="Huzzah-01")
    s1.legend.location = "bottom_left"

    s2 = figure(title='Humidity', plot_width=800, plot_height=500, x_axis_type='datetime')
    s2.line(x=df3.index, y=df3['Shed Humidity'], color="navy", legend="Shed %H")
    s2.line(x=df4.index, y=df4['Huzzah-01 Humidity'], color="green", legend="Huzzah-01 %H")
    s2.line(x=df6.index, y=df6['Huzzah-01 Pressure (Hg)'], color="purple", legend="Huzzah-01 (Hg)")
    s2.legend.location = "bottom_left"

    s3 = figure(title='Voltage%', plot_width=800, plot_height=250, x_axis_type='datetime')
    s3.line(x=df5.index, y=df5['Huzzah-01 Voltage'], color="red", legend="Huzzah-01")
    # s3.line(x=df4.index, y=df4['Huzzah-01 Humidity'], color="green", legend="Huzzah-01")
    s3.legend.location = "bottom_left"

    output_file("/usr/share/nginx/html/index.html")

    # put all the plots together
    save(column(s1, s2, s3))


if __name__ == '__main__':
    try:
        main_run()
    except:
        traceback.print_exc()
