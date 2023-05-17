# -*- coding: utf-8 -*-
"""
Created on Thu May  4 18:10:03 2023

@author: Ted
"""
import ctypes as ct

#example data
dataString = "320254641900E72A8884811B2FFF36883E0254641900F52A8784811B36FF3685480254641A00F82A8A84811B34BF3685"
dataBytes = bytes.fromhex(dataString)
nBytes = len(dataBytes)

#recreate the data structure from the Arduino data logger
class single_record(ct.LittleEndianStructure):
    _fields_ = [("logtime", ct.c_uint32, 32),
                ("tuBackground", ct.c_uint32, 16),
                ("tuBackscatter", ct.c_uint32, 16),
                ("waterPressure", ct. c_uint32, 21),
                ("waterTemp",ct.c_int32, 11),
                ("baroAnomaly",ct.c_int32,14),
                ("airTemp",ct.c_int32,10),
                ("batteryVoltage",ct.c_uint32,8)]    
    
class transmission_packet(ct.Union):
    _fields_ = [("record", single_record*(nBytes//sizeof(single_record))),
                ("data", ct.c_ubyte*nBytes)]
    

#Create a packet object and put our data in as a byte array.
packet = transmission_packet()
packet.data = (c_ubyte * nBytes)(*dataBytes)

#example data access
print(packet.record[0].logtime)

# %%
for r in packet.record:
    print(f"unix time:      {r.logtime}\n"
          f"background:     {r.tuBackground}\n" 
          f"backscatter:    {r.tuBackscatter}\n"
          f"water P:        {r.waterPressure}\n"
          f"water temp:     {r.waterTemp}\n"
          f"baro P:         {1E5+r.baroAnomaly}\n"
          f"air temp:       {r.airTemp}\n"
          f"battery V:      {r.batteryVoltage}\n")
    
    

"""
from Arduino to help verify.
record[0]
time:		1683227186
background:	25
reading:	10983
hydro p:	99464
water temp:	220
air p:		-209
air temp:	219
battery:	136


record[1]
time:		1683227198
background:	25
reading:	10997
hydro p:	99463
water temp:	220
air p:		-202
air temp:	219
battery:	133


record[2]
time:		1683227208
background:	26
reading:	11000
hydro p:	99466
water temp:	220
air p:		-204
air temp:	218
battery:	133
    
320254641900E72A8884811B2FFF36883E0254641900F52A8784811B36FF3685480254641A00F82A8A84811B34BF3685

"""



