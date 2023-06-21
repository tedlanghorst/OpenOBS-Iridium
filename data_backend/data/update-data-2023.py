#!/usr/bin/python3.10

import os
import glob
import csv
import ctypes as ct
os.environ['MPLCONFIGDIR'] = "/tmp/"
import matplotlib.pyplot as plt
from datetime import datetime, timedelta
import pandas as pd

nBytes = 48

os.umask(0)
def opener(path,flags):
    return os.open(path,flags,0o775)

"""
turns out this is pretty complicated, depends on atmospheric pressure, 
dissolved gasses, isotopes, salinity, yadda yadda. Temperature is most
important within normal ranges.
formula taken from here:
Tanaka, M., et al. "Recommended table for the density of water between 0 C 
and 40 C based on recent experimental reports." Metrologia 38.4 (2001): 301.
"""
def getDensity(t):
    a1 = -3.98305
    a2 = 301.797
    a3 = 522528.9
    a4 = 69.34881
    a5 = 999.974950
    return a5*(1-((t+a1)**2*(t+a2))/(a3*(t+a4)))

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
    _fields_ = [("record", single_record*3),
                ("data", ct.c_ubyte*nBytes)]
   

rawDir = '/Users/Ted/Documents/IridiumDump/raw'
unpackedDir = '/Users/Ted/Documents/IridiumDump/unpacked'   
assetDir = '/Users/Ted/Documents/IridiumDump/full-files'   
# rawDir = '/srv/data/IridiumDump/raw'
# unpackedDir = '/srv/data/IridiumDump/unpacked'
# assetDir = '/var/www/html/assets'

rawFiles = glob.glob(os.path.join(rawDir,'*/*.csv'))
unpackedFiles = glob.glob(os.path.join(unpackedDir,'*/*.csv'))


for rawFile in rawFiles:
    #check if a matching file already exists in the unpacked directory
    unpackedFile = os.path.join(unpackedDir,os.path.relpath(rawFile,rawDir))
    if os.path.exists(unpackedFile):
        continue #to next raw file
    
    #read in raw data from line 7
    with open(rawFile) as file:
        for index, line in enumerate(iter(file)):
            if index == 6:  
                dataString = line
    if len(dataString) == 0:
        continue #could log this somewhere. For now just skip.
    
    dataBytes = bytes.fromhex(dataString)
    
    #make sure the data matches our structure
    if len(dataBytes) != nBytes:
        continue #could log this somewhere. For now just skip.
      
    #Create a packet object and put our data in as a byte array.
    packet = transmission_packet()
    packet.data = (ct.c_ubyte * nBytes)(*dataBytes)
    
    #make a directory for the ROCKBLOCK SN if needed
    sn_dir = os.path.split(unpackedFile)[0]
    if not os.path.exists(sn_dir):
        os.makedirs(sn_dir,mode=0o775)
    
    #create the unpacked data file
    labels = [f[0] for f in single_record._fields_]
    with open(unpackedFile,'w',opener=opener,newline='') as file:
        write = csv.writer(file)
        write.writerow(labels)
        for r in packet.record:
            write.writerow([getattr(r,l) for l in labels])
            
    #Now we are going to make the data more friendly and merge them into one file
    sn = os.path.split(sn_dir)[1]
    longFile = os.path.join(assetDir,sn+'.csv')
    
    #make a new file headers if needed
    if not os.path.exists(longFile):
        with open(longFile,'w',opener=opener,newline='') as file:
            write = csv.writer(file)
            write.writerow(['time','ambient','backscatter','waterPressure',
                            'waterTemp','airPressure','airTemp',
                            'batteryVoltage','waterDepth'])
    
    #append friendly data to our big record
    with open(longFile,'a',newline='') as file:
        write = csv.writer(file)
        for r in packet.record:
            dt = datetime.utcfromtimestamp(r.logtime)
            time = dt.strftime('%Y-%m-%d %H:%M:%S')
            P_w = r.waterPressure/1E5
            T_w = r.waterTemp/10
            P_a = 1 + r.baroAnomaly/1E5
            T_a = r.airTemp/10
            bV = r.batteryVoltage/10
            density = getDensity(T_w)
            depth = 1E5*(P_w-P_a)/(density*9.80665)
            write.writerow([time,r.tuBackground,r.tuBackscatter,
                            P_w,T_w,P_a,T_a,bV,depth])
            

fullFiles = glob.glob(os.path.join(assetDir,'*.csv'))
xRange = [datetime.now()-timedelta(weeks=1), datetime.now()]
for p in fullFiles:
    plt.close('all')
    sn = os.path.basename(p)[0:-4]
    d = pd.read_csv(p,parse_dates=['time'],)
    depthAxis = plt.subplot(4,1,1)
    depthAxis.plot(d.time,d.waterDepth,'r.')
    plt.ylabel("Water\ndepth (m)")
    plt.xlim(xRange)

    # if sn in nameDict:
    #     plt.title(f"{nameDict[self.sn]}")
    # else:
    plt.title(f"Iridium SN: {sn}.")

    turbAxis = plt.subplot(4,1,2, sharex=depthAxis)
    turbAxis.plot(d.time,d.backscatter,'b.')
    plt.ylabel("Backscatter")
    plt.ylim([3000,10000])
   
    tempAxis = plt.subplot(4,1,3, sharex=depthAxis)
    tempAxis.plot(d.time,d.waterTemp,'b.',label='water')
    tempAxis.plot(d.time,d.airTemp,'k.',label='air')
    plt.legend(loc='upper left')
    plt.ylabel("Temp (C)")

    battAxis = plt.subplot(4,1,4, sharex=depthAxis)
    battAxis.plot(d.time,d.batteryVoltage,'k.')
    plt.ylabel("Battery\nVoltage")
    plt.ylim([11.5,14.5])
           
    fig = plt.gcf()
    fig.set_size_inches(4, 6)
    fig.autofmt_xdate()
    plt.tight_layout()
    fig.savefig(f'{assetDir}/{sn}.png', dpi=300)
    
    