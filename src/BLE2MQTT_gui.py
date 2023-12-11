from tkinter import ttk
import tkinter as tk
import paho.mqtt.client as mqtt
import datetime
import subprocess, platform
import re

debug = False
sensor = {"device": []}
sensorframe = {}
sensorwidget ={}
bigfont = ("Helvetica", "12")
valfont = ("Helvetica", "22")

def debugprint(msg):
        now = datetime.datetime.now()
        msg = now.strftime("%Y.%m.%d %H:%M:%S.%f") + ": " + msg
        try:
            debuglog.insert('end', msg + '\n')
            debuglog.see('end')
        except:
            pass
        print(msg)
        
def addsensor(dev):
    debugprint("Add Sensor:" + dev)
    sensorframe[dev] = tk.Frame(sensorcontainer, padx=2, pady=2, bd=1, relief="solid")
    sensorwidget[dev + "_device_lbl"] = tk.Label(sensorframe[dev], bg="white", justify="left", font=bigfont, text="Sensor ID: " + dev)
    sensorwidget[dev + "_device_lbl"].grid(row=0, column=0, sticky="EW", columnspan = 2)
    #sensorwidget[dev + "_device_val"] = tk.Label(sensorframe[dev], bg="white", font=bigfont, text=dev)
    #sensorwidget[dev + "_device_val"].grid(row=0, column=1, sticky="EW")
    sensorcontainer.window_create("end", window=sensorframe[dev])
    
def addwidget(dev,field):
    key = dev + "_" + field
    if not key + "_lbl" in sensorwidget.keys():
        r = sensor[dev]['count']
        sensorwidget[key + "_lbl"] = tk.Label(sensorframe[dev], font=bigfont, justify="left", text = field)
        sensorwidget[key + "_lbl"].grid(row = r, column = 0, sticky="W")
        sensorwidget[key + "_val"] = tk.Label(sensorframe[dev], font=bigfont, text = sensor[dev][field])
        sensorwidget[key + "_val"].grid(row = r, column = 1, sticky="EW")

def updatewidget(dev,field):
    key = dev + "_" + field + "_val"
    sensorwidget[key].config(text = sensor[dev][field])
    
def reconfigureLayout(dev):
    if sensor[dev]['type'] in "ThermoBeacon,Govee H5075":
        #debugprint("Reconfigure " + sensor[dev]['type'])
        sensorwidget[dev+"_fullname_val"].config(bg="white")
        sensorwidget[dev+"_fullname_val"].grid(row=0, column=0, columnspan=2, sticky="EW")
        sensorwidget[dev+"_fullname_lbl"].config(text = "Device ID")
        sensorwidget[dev+"_device_lbl"].config(text = dev, bg="white smoke")
        sensorwidget[dev+"_device_lbl"].grid(row=8, column=1, columnspan=1)
        sensorwidget[dev+"_temp_lbl"].lower()
        sensorwidget[dev+"_temp_val"].config(fg="dark orange", font=valfont)
        sensorwidget[dev+"_temp_val"].grid(row=1, column=0, rowspan=2, sticky="EW")
        sensorwidget[dev+"_hum_lbl"].lower()
        sensorwidget[dev+"_hum_val"].config(fg="DeepSkyBlue3", font=valfont)
        sensorwidget[dev+"_hum_val"].grid(row=1, column=1, rowspan=2, sticky="EW")
        
def printsensors():
    for d in sensor:
        print ("Sensor: " + d)
        s = sensor[d]
        for key in s:
            print("  " + key + " => " + s[key])

def on_message(client, userdata, message):
    try:
        payload = str(message.payload.decode("utf-8"))
    except:
        payload = "error"
        
    try:
        topic = message.topic      
    except:
        topic="error"    
    
    if debug:    
        debugprint("message topic=",topic)    
        debugprint("message received " , payload)
        debugprint("message qos=",message.qos)
        debugprint("message retain flag=",message.retain)
    
    if message.retain == 0:
        now = datetime.datetime.now()
        mqttlog.insert('end', now.strftime("%Y.%m.%d %H:%M:%S.%f") + ": " + topic + ':' + payload + '\n')
        mqttlog.see('end')
        
    if message.retain == 1 and '/openhab/debug/ble2mqtt-' in topic:
        newdev = topic.split('/')[3]
        devices.append(newdev)
        cbDevices['values'] = devices
        cbDevices.set(newdev)
        debugprint("Found Device: " + topic.split('/')[3])
    
    m = re.match("^\/openhab\/in\/([0-9,a-f]{12})_(.+)\/state",topic)
    if message.retain == 0 and m:
        (dev,field)  = m.groups()
        if not dev in sensor.keys():
            debugprint("Found Sensor: " + dev)
            sensor[dev] = {'count': 0}
            addsensor(dev)
                       
        sensor[dev][field] = payload
        key = dev + "_" + field
        if not key + "_lbl" in sensorwidget.keys():
            sensor[dev]['count'] = sensor[dev]['count'] + 1
            addwidget(dev,field)         
        else:
            updatewidget(dev,field)
            if "type" in field:
                reconfigureLayout(dev)
    
def publishcmd(command):
    dev = cbDevices.get()
    if dev != '':
        testtopic = "/openhab/configuration/" + dev
        if debug:
            debugprint(testtopic + " => " + command)
        try:
            mqclient.publish(testtopic +  "/cmd", command)
        except Exception as e:
            debugprint ("MQ send failed: {}".format(e))
    
def bgetIPcallback():
    publishcmd("getIP")
    
def bgetVersioncallback():
    publishcmd("getVersion") 
    
def bsetScreenPluscallback():
    publishcmd("setScreen+") 
    
def bsetScreenMinuscallback():
    publishcmd("setScreen-") 

def brestartcallback():
    publishcmd("restart") 
    
def breconfigcallback():
    publishcmd("reconfig")

def getWiFi():
    output = "none"
    # Get the name of the operating system.
    os_name = platform.system()
    # Check if the OS is Windows.
    if os_name == "Windows":
        # Command to list Wi-Fi networks on Windows using netsh.
        list_networks_command = 'netsh wlan show networks'
        # Execute the command and capture the result.
        netcmd = subprocess.Popen(list_networks_command, shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE )
        output, error = netcmd.communicate()
        output = output.decode("utf-8", "ignore")
        #print(output)
    # Check if the OS is Linux.
    elif os_name == "Linux":
        # Command to list Wi-Fi networks on Linux using nmcli.
        list_networks_command = "nmcli device wifi list"
        # Execute the command and capture the output.
        output = subprocess.check_output(list_networks_command, shell=True, text=True)
        # Print the output, all networks in range.
        print(output)
        # Handle unsupported operating systems.
    else:
        # Print a message indicating that the OS is unsupported (Not Linux or Windows).
        print("Unsupported OS")
        
    return output

# ************************* Main ****************************

ssid = getWiFi()

if "UPC4E87B2D" in ssid:
    mqttBroker ="192.168.20.17"
else:
    mqttBroker ="82.165.176.152"

mqclient = mqtt.Client("DesktopGUI")
mqclient.connect(mqttBroker)

devices = list()

basetopic = "/openhab/in/#"
conftopic = "/openhab/configuration/#"
debugtopic = "/openhab/debug/#"

mqclient.on_message=on_message

window = tk.Tk()
window.title("BLE2MQTT GUI")

greeting = tk.Label(text="Connected to MQTT-Broker: " + mqttBroker)
greeting.pack()

buttonframe = tk.Frame()
cbDevices = ttk.Combobox(buttonframe, width=25, state="readonly")
cbDevices.pack(padx=5, pady=2, side="left")
bgetIP = tk.Button(buttonframe, text="getIP", padx=10, command=bgetIPcallback)
bgetIP.pack(padx=2, pady=2, side="left")
bgetVersion = tk.Button(buttonframe, text="getVersion", padx=10, command=bgetVersioncallback)
bgetVersion.pack(padx=2, pady=2, side="left")
bsetScreenMinus = tk.Button(buttonframe, text="Screen -", padx=10, command=bsetScreenMinuscallback)
bsetScreenMinus.pack(padx=2, pady=2, side="left")
bsetScreenPlus = tk.Button(buttonframe, text="Screen +", padx=10, command=bsetScreenPluscallback)
bsetScreenPlus.pack(padx=2, pady=2, side="left")
brestart = tk.Button(buttonframe, text="Restart", padx=10, command=brestartcallback)
brestart.pack(padx=2, pady=2, side="right")
breconfig= tk.Button(buttonframe, text="Reconfigure", padx=10, command=breconfigcallback)
breconfig.pack(padx=2, pady=2, side="right")
buttonframe.pack(fill="x")

tabControl = ttk.Notebook(window)
mqtttab = ttk.Frame(tabControl)
logtab = ttk.Frame(tabControl)
sensortab = ttk.Frame(tabControl)

tabControl.add(mqtttab, text=' MQTT Messages ')
tabControl.add(sensortab, text=' Sensors ')
tabControl.add(logtab, text=' Debug Log ')
tabControl.pack(expand=1, fill="both")

sensorcontainer = tk.Text(sensortab, wrap="char", borderwidth=0, highlightthickness=0, state="disabled", cursor="arrow") 
sensorvbs = tk.Scrollbar(sensortab,orient="vertical", command = sensorcontainer.yview)
sensorcontainer.configure(yscrollcommand=sensorvbs.set)
sensorvbs.pack(side="right", fill="y")
sensorcontainer.pack(side="left", fill="both", expand=True)

mqttlog = tk.Text(mqtttab, height = 60,  width = 120)
mqttvbs = tk.Scrollbar(mqtttab,orient="vertical", command = mqttlog.yview)
mqttlog.configure(yscrollcommand=mqttvbs.set)
mqttvbs.pack(side="right", fill="y")
mqttlog.pack(side="left", fill="both", expand=True)

debuglog = tk.Text(logtab, height = 60,  width = 120)
debugvbs = tk.Scrollbar(logtab,orient="vertical", command = debuglog.yview)
debuglog.configure(yscrollcommand=debugvbs.set)
debugvbs.pack(side="right", fill="y")
debuglog.pack(side="left", fill="both", expand=True)

debugprint("Start Loop...")
mqclient.loop_start()
debugprint("Subscribe...")
mqclient.subscribe([(conftopic,0),(debugtopic,0),(basetopic,0)])
window.mainloop()
mqclient.loop_stop()

debugprint("Stop Loop...")

