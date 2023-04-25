#!/usr/bin/python3.10

import glob
import csv
import re
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
import smtplib
from email.message import EmailMessage
from email.utils import make_msgid

# You will have to update these details if you want to send emails. This is really not a good way to do this, but it is quick.
# For security purposes, I recommend setting up a new gmail account and then getting an app-specific password for this script.
send_from = 
send_pass = 
send_to = 

assetDir = "/var/www/html/assets"

nameDict = {209175: "Tanana Lakes",
            210571: "Sam Charley",
            211590: "Jonas\' house"}

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
        self.lastTransmission = ""
        self.lastBattV = 0
        
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
        self.lastTransmission = max(self.time).strftime('%Y-%m-%d %H:%M:%S')
        
    def __repr__(self):
        outString = f"Serial Number:\t{self.sn}\n"
        outString += f"Last transmission: {self.lastTransmission}\n"
        outString += f"Battery Voltage:\t{self.battV[-1]:.2f}V\n"
        outString += "time\t\t\t\t\tdepth\tambient\tbackscatter\ttemp\n"
        for i in range(-3,0):
            outString += f"{self.timeString[i]}\t\t{self.depth[i]:.2f}m\t" 
            outString += f"{self.ambient[i]}\t\t{self.backscatter[i]}\t\t{self.temp[i]:.2f}C\n"
        return outString
    
    def filterStartDate(self,startDate):
        mask = [t>=startDate for t in self.time]
        self.time = self.time[mask]
        self.timeString = self.timeString[mask]
        self.pressure = self.pressure[mask]
        self.depth = self.depth[mask]
        self.ambient = self.ambient[mask]
        self.backscatter = self.backscatter[mask]
        self.temp = self.temp[mask]
        self.battV = self.battV[mask]
    
    
    def plot(self,depthMin,scatterMax):
        mask = [d>depthMin for d in self.depth]
        depthAxis = plt.subplot(4,1,1)
        depthAxis.plot(self.time[mask],self.depth[mask],'r.')
        plt.ylabel("Appx. water\ndepth (m)")
        depthAxis.get_xaxis().set_ticks([])
         
        if self.sn in nameDict:
            plt.title(f"Data from {nameDict[self.sn]}.")
        else:
            plt.title(f"Data from Iridium SN: {self.sn}.")

        mask = [b<scatterMax for b in self.backscatter]
        turbAxis = plt.subplot(4,1,2)
        turbAxis.plot(self.time[mask],self.backscatter[mask],'b.')
        plt.ylabel("Backscatter")
        turbAxis.get_xaxis().set_ticks([])
        
        tempAxis = plt.subplot(4,1,3)
        tempAxis.plot(self.time,self.temp,'k.')
        plt.ylabel("Temp (C)")
        tempAxis.get_xaxis().set_ticks([])


        battAxis = plt.subplot(4,1,4)
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

transmission_count = 0
for singleFile in fileList:
    with open(singleFile) as csv_file:
        csv_reader = csv.reader(csv_file, delimiter=',')
        found_headers = False
        for row in csv_reader: 
            if "serial" in row[0]:
                deviceSN = int(re.search("[0-9]+",row[0])[0])
            elif "latitude" in row[0]:
                lat = re.search("[-0-9.]+",row[0])[0]
            elif "longitude" in row[0]:
                lon = re.search("[-0-9.]+",row[0])[0]
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
    transmission_count += 1
    
print(f'Processed {transmission_count} transmissions.')

#write the data out to a text file
for obs in loggers:
    obs.filterStartDate(datetime(2022,10,3))
    with open(f"{assetDir}/{obs.sn}.csv","w",newline="") as file:
        file.write(f"serial number: {obs.sn}\n")
        file.write(f"lat: {obs.lat}\n")
        file.write(f"lon: {obs.lon}\n")
        file.write("time,pressure (mbar),approximate depth (m),ambient,backscatter," +
                    "temp (C),battery (V)")
        for i in range(0,len(obs.time)):
            file.write(f"\n{obs.timeString[i]},{obs.pressure[i]},{obs.depth[i]},"+
                        f"{obs.ambient[i]},{obs.backscatter[i]},{obs.temp[i]},{obs.battV[i]}")

    plt.close("all")
    obs.plot(0.1,10000)


dateString = datetime.now().strftime("%B %d, %Y")
timeString = datetime.now().strftime("%B %d, %Y  %H:%M")
plotPaths = glob.glob(f'{assetDir}/*.png')

lastReportText = ""
cidText = ""
obs_cid = list()
for l in loggers:
    obs_cid.append(make_msgid())
    lastReportText += f"""
    Serial Number: {l.sn} 
    Last transmission: {l.lastTransmission}
    Battery Voltage: {l.battV[l.time==max(l.time)][0]:0.2f} V
    Appx. Depth: {l.depth[l.time == max(l.time)][0]:0.2f} m
    """
    cidText += f"<img src=\"cid:{obs_cid[-1][1:-1]}\" />"
    

msg = EmailMessage()
msg['Subject'] = 'Live From The Tanana: '+dateString
msg['From'] = send_from
msg['To'] = send_to
msg.set_content(f"""\
Iridium logger report {timeString}

{lastReportText}

""")

# Add the html version.  This converts the message into a multipart/alternative
# container, with the original text message as the first part and the new html
# message as the second part.
htmlLastReport = lastReportText.replace("\n","<br />")

asparagus_cid = make_msgid()
msg.add_alternative(f"""\
<html>
  <head>Iridium logger report {timeString}</head>
  <body>
    <p>
        {htmlLastReport}
    </p>
    {cidText}
  </body>
</html>
""", subtype='html')
# <img src="cid:{asparagus_cid[1:-1]}" />
# note that we needed to peel the <> off the msgid for use in the html.

# Now add the related image to the html part.
for [path,cid] in zip(plotPaths,obs_cid):
    with open(path, 'rb') as img:
        msg.get_payload()[1].add_related(img.read(), 'image', 'jpeg',
                                         cid=cid)
    
try:
    server = smtplib.SMTP_SSL('smtp.gmail.com', 465)
    server.ehlo()
    server.login(send_from, send_pass)
    server.send_message(msg)
    server.close()

    print('Email sent!')
except:
    print('Something went wrong...')
   


