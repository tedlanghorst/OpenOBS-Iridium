#!/usr/bin/python
import cgi
import os
import logging
import csv
import struct
from datetime import datetime

dataPath = "/srv/data"
timeString = datetime.now().strftime('%Y%m%d%H%M%S')
form = cgi.FieldStorage()

try: 
    imei = form.getvalue("imei")
    serial = form.getvalue("serial")
    momsn = form.getvalue("momsn")
    transmit_time = form.getvalue("transmit_time")
    iridium_latitude = form.getvalue("iridium_latitude")
    iridium_longitude = form.getvalue("iridium_longitude")
    iridium_cep = form.getvalue("iridium_cep")
    hexData = form.getvalue("data")

    if imei is not None:
        #Respond with a success message
        print("Content-Type:text/html\n\n")
        print("OK") 

    #Interpret the data as bytes.
    byteArray = bytes.fromhex(hexData)

    #nibble the first two bytes as the battery int.
    batteryVolts = (byteArray[1]<<8 | byteArray[0]) / 1023 * 5
    byteArray = byteArray[:-2] #first time is incorrect because of bad byte packing (overlapping time and volts). We can infer from the second two times.

    #Create iterable for unpacking one line of data at a time.
    #Will catch here if the data format is incorrect.
    unpacker = struct.iter_unpack("<2L2Hh",byteArray)
    records = [list(line) for line in unpacker]
    
    #first record's time is corrupted by the battery bytes, so we fix it using the second and third
    time0 = records[1][0] - (records[2][0]-records[1][0])
    records[0][0] = time0


    filePath = f"{dataPath}/{serial}"
    if not os.path.exists(filePath):
        os.makedirs(filePath)

    fileName = filePath+f"/{timeString}.csv"
    labels = ["UnixTime","Pressure[bar_1E-4]","Ambient[DN]","Backscatter[DN]","WaterTemp[C_1E-2]"]
    with open(fileName,'w') as file:
        write = csv.writer(file)
        write.writerow([f"imei: {imei}"])
        write.writerow([f"serial: {serial}"])
        write.writerow([f"transmit time: {transmit_time}"])
        write.writerow([f"latitude: {iridium_latitude}"])
        write.writerow([f"longitude: {iridium_longitude}"])
        write.writerow([f"cep: {iridium_cep}"])
        write.writerow([f"battery: {batteryVolts}"])
        write.writerow(labels)
        write.writerows(records)    

except:
    #log the raw input data if anything went wrong.
    with open(dataPath+'/log.txt','a') as file:
        file.write(f"{timeString}: {str(form)}\n\n")
