#!/usr/bin/python3.10

import os
import glob
import csv
import re
import numpy as np
os.environ['MPLCONFIGDIR'] = "/tmp/"
import matplotlib.pyplot as plt
from datetime import datetime

assetDir = '/var/www/html/assets'

nameDict = {209175: "Tanana Lakes",
            210571: "Sam Charley",
            211590: "Jonas house"}

class OBS:
    def __init__(self,sn,lat,lon):
        self.sn = sn
        self.lat = lat
        self.lon = lon
        self.time = []
        self.timeString = []
        self.pressure = []
        self.depth = []
        self.ambient = []
        self.backscatter = []
        self.temp = []
        self.battV = []
        
    def addData(self, dataRow):        
        time = datetime.utcfromtimestamp(dataRow[0])
        timeString = time.strftime('%Y-%m-%d %H:%M:%S')
        depth = (dataRow[1]/1E4 - 1) * 10.1972 #rough conversion from bar*10^-4 to water depth   
        
        self.time = np.append(self.time,time)
        self.timeString = np.append(self.timeString,timeString)
        self.pressure = np.append(self.pressure,dataRow[1]/10)
        self.depth = np.append(self.depth,depth)
        self.ambient = np.append(self.ambient,dataRow[2])
        self.backscatter = np.append(self.backscatter,dataRow[3])
        self.temp = np.append(self.temp,dataRow[4]/100)  
        self.battV = np.append(self.battV,dataRow[5])
        

    def applySubset(self,mask):
        self.time = self.time[mask]
        self.timeString = self.timeString[mask]
        self.pressure = self.pressure[mask]
        self.depth = self.depth[mask]
        self.ambient = self.ambient[mask]
        self.backscatter = self.backscatter[mask]
        self.temp = self.temp[mask]
        self.battV = self.battV[mask]
    
    
    def plotAndSave(self,xRange,depthMin,scatterMax):
        mask = [d>depthMin for d in self.depth]
        depthAxis = plt.subplot(4,1,1)
        depthAxis.plot(self.time[mask],self.depth[mask],'r.')
        plt.ylabel("Appx. water\ndepth (m)")
        plt.xlim(xRange)

        if self.sn in nameDict:
            plt.title(f"{nameDict[self.sn]}")
        else:
            plt.title(f"Iridium SN: {self.sn}.")

        mask = [b<scatterMax for b in self.backscatter]
        turbAxis = plt.subplot(4,1,2, sharex=depthAxis)
        turbAxis.plot(self.time[mask],self.backscatter[mask],'b.')
        #turbAxis.set_yscale("log")
        plt.ylabel("Backscatter")
       
        tempAxis = plt.subplot(4,1,3, sharex=depthAxis)
        tempAxis.plot(self.time,self.temp,'k.')
        plt.ylabel("Temp (C)")

        battAxis = plt.subplot(4,1,4, sharex=depthAxis)
        battAxis.plot(self.time,self.battV,'k.')
        plt.ylabel("Battery\nVoltage")
               
        fig = plt.gcf()
        fig.set_size_inches(4, 6)
        fig.autofmt_xdate()
        plt.tight_layout()
        fig.savefig(f'{assetDir}/{self.sn}.png', dpi=300)
    
        return [depthAxis,turbAxis,battAxis]
        

fileList = glob.glob('/srv/data/*/*.csv')
loggers = list()

for singleFile in fileList:
    with open(singleFile) as csv_file:
        csv_reader = csv.reader(csv_file, delimiter=',')
        found_headers = False
        for row in csv_reader: 
            if "serial" in row[0]:
                deviceSN = int(re.search("[0-9]+",row[0])[0])
            elif "latitude" in row[0]:
                lat = float(re.search("[-0-9.]+",row[0])[0])
            elif "longitude" in row[0]:
                lon = float(re.search("[-0-9.]+",row[0])[0])
            elif "battery" in row[0]:
                battString = re.search("[-0-9.]+",row[0])[0]
                battV = round(float(battString),2)
            elif "UnixTime" in row[0]:
                found_headers = True #done with the headers
                #now create or find the appropriate logger variable and add the data
                snMatch = [l.sn==deviceSN for l in loggers]
                if any(snMatch):
                    snIdx = int(np.linspace(0,len(snMatch)-1,len(snMatch))[snMatch])
                else:
                    loggers.append(OBS(deviceSN,lat,lon))
                    snIdx = len(loggers)-1
            elif found_headers:
                dataRow = [int(r) for r in row]
                dataRow.append(battV)
                loggers[snIdx].addData(dataRow)


#write the data out to a text file
xRange = [datetime(2022,10,3), datetime.now()]
for obs in loggers:
    sortIdx = obs.time.argsort()
    obs.applySubset(sortIdx)
    dateMask = [t>=xRange[0] for t in obs.time]    
    obs.applySubset(dateMask)
    with open(f"{assetDir}/{obs.sn}.csv","w",newline="") as file:
        file.write(f"serial number: {obs.sn}\n")
        file.write(f"lat: {obs.lat:0.2f}\n")
        file.write(f"lon: {obs.lon:0.2f}\n")
        file.write("time,pressure (mbar),approximate depth (m),ambient,backscatter," +
                    "temp (C),battery (V)")
        for i in range(0,len(obs.time)):
            file.write(f"\n{obs.timeString[i]},{obs.pressure[i]},{obs.depth[i]:0.2f},"+
                    f"{obs.ambient[i]},{obs.backscatter[i]},{obs.temp[i]},{obs.battV[i]}")

    lastReportText = f"""LAST TRANSMISSION
    Serial:       {obs.sn} 
    Time (AKDT):  {obs.time[-1].strftime('%Y-%m-%d %H:%M:%S')}
    Pressure:     {obs.pressure[-1]:0.1f} mbar
    Appx. Depth:  {obs.depth[-1]:0.2f} m
    Ambient:      {obs.ambient[-1]:0.0f}
    Backscatter:  {obs.backscatter[-1]:0.0f}
    Temperature:  {obs.temp[-1]:0.2f} C
    Battery:      {obs.battV[-1]:0.2f} V"""
    
    with open(f"{assetDir}/{obs.sn}_status.txt","w",newline="") as file:
        file.write(lastReportText)

    plt.close("all")
    obs.plotAndSave(xRange,0,100000)


