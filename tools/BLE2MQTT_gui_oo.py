from tkinter import ttk
from tkinter import messagebox
import tkinter as tk
import paho.mqtt.client as mqtt
import datetime
import subprocess, platform
import re
import time

debug = False
sensor = {"device": []}

basetopic = "/openhab/in/#"
conftopic = "/openhab/configuration/#"
debugtopic = "/openhab/debug/#"

devices = list()
window = tk.Tk()
isOpen = False

FIRST_RECONNECT_DELAY = 1
RECONNECT_RATE = 2
MAX_RECONNECT_COUNT = 12
MAX_RECONNECT_DELAY = 60

class FaultTolerantTk(tk.Tk):
    def report_callback_exception(self, exc, val, tb):
        self.destroy_unmapped_children(self)
        messagebox.showerror('Error!', val)

    # NOTE: It's an optional method. Add one if you have multiple windows to open
    def destroy_unmapped_children(self, parent):
        """
        Destroys unmapped windows (empty gray ones which got an error during initialization)
        recursively from bottom (root window) to top (last opened window).
        """
        children = parent.children.copy()
        for index, child in children.items():
            if not child.winfo_ismapped():
                parent.children.pop(index).destroy()
            else:
                self.destroy_unmapped_children(child)
class App:
    
    medfont = ("Helvetica", "12")
    valfont = ("Helvetica", "22")

    def __init__(self, master):
        self.sensorframe = {}
        self.sensorwidget = {}
        style = ttk.Style(master)

        # Set the theme with the theme_use method
        # style.theme_use('aqua')  # put the theme name here, that you want to use

        self.greeting = tk.Label(text="Connected to MQTT-Broker: " + mqttBroker)
        self.greeting.pack()

        buttonframe = tk.Frame()
        self.cbDevices = ttk.Combobox(buttonframe, width=25, state="readonly")
        self.cbDevices.pack(padx=5, pady=2, side="left")
        self.bgetIP = tk.Button(buttonframe, text="getIP", padx=10, command=bgetIPcallback)
        self.bgetIP.pack(padx=2, pady=2, side="left")
        self.bgetVersion = tk.Button(buttonframe, text="getVersion", padx=10, command=bgetVersioncallback)
        self.bgetVersion.pack(padx=2, pady=2, side="left")
        self.bsetScreenMinus = tk.Button(buttonframe, text="Screen -", padx=10, command=bsetScreenMinuscallback)
        self.bsetScreenMinus.pack(padx=2, pady=2, side="left")
        self.bsetScreenPlus = tk.Button(buttonframe, text="Screen +", padx=10, command=bsetScreenPluscallback)
        self.bsetScreenPlus.pack(padx=2, pady=2, side="left")
        self.bblankScreen = tk.Button(buttonframe, text="Screen Off", padx=10, command=bblankScreenCallback)
        self.bblankScreen.pack(padx=2, pady=2, side="left")
        self.bdebug = tk.Button(buttonframe, text="Toggle Debug", padx=10, command=bdebugCallback)
        self.bdebug.pack(padx=2, pady=2, side="left")

        self.brestart = tk.Button(buttonframe, text="Restart", padx=10, command=brestartcallback)
        self.brestart.pack(padx=2, pady=2, side="right")
        self.breconfig= tk.Button(buttonframe, text="Reconfigure", padx=10, command=breconfigcallback)
        self.breconfig.pack(padx=2, pady=2, side="right")
        buttonframe.pack(fill="x")

        tabControl = ttk.Notebook(master)
        #conntab = ttk.Frame(tabControl)
        mqtttab = ttk.Frame(tabControl)
        logtab = ttk.Frame(tabControl)
        sensortab = ttk.Frame(tabControl)

        #tabControl.add(conntab, text=' Connection ')
        tabControl.add(mqtttab, text=' MQTT Messages ')
        tabControl.add(sensortab, text=' Sensors ')
        tabControl.add(logtab, text=' Debug Log ')
        tabControl.pack(expand=1, fill="both")

        #bconnect = tk.Button(conntab, text="Connect", command=bConnectCallback)
        #bconnect.pack(padx=10, pady=50, anchor='center')

        self.sensorcontainer = tk.Text(sensortab, wrap="char", borderwidth=0, highlightthickness=0, state="disabled", 
                                       cursor="arrow") 
        sensorvbs = tk.Scrollbar(sensortab,orient="vertical", command = self.sensorcontainer.yview)
        self.sensorcontainer.configure(yscrollcommand=sensorvbs.set)
        sensorvbs.pack(side="right", fill="y")
        self.sensorcontainer.pack(side="left", fill="both", expand=True)

        self.mqttlog = tk.Text(mqtttab, height = 60,  width = 120)
        mqttvbs = tk.Scrollbar(mqtttab,orient="vertical", command = self.mqttlog.yview)
        self.mqttlog.configure(yscrollcommand=mqttvbs.set)
        mqttvbs.pack(side="right", fill="y")
        self.mqttlog.pack(side="left", fill="both", expand=True)

        self.debuglog = tk.Text(logtab, height = 60,  width = 120)
        debugvbs = tk.Scrollbar(logtab,orient="vertical", command = self.debuglog.yview)
        self.debuglog.configure(yscrollcommand=debugvbs.set)
        debugvbs.pack(side="right", fill="y")
        self.debuglog.pack(side="left", fill="both", expand=True)

    def addsensor(self,dev):
        if debug:
            debugprint("Add Sensor:" + dev)
        self.sensorframe[dev] = tk.Frame(self.sensorcontainer, padx=2, pady=2, bd=1, relief="solid")
        self.sensorwidget[dev + "_device_lbl"] = tk.Label(self.sensorframe[dev], bg="white", fg="black", justify="left", 
                                                          font=self.medfont, text="Sensor ID: " + dev)
        self.sensorwidget[dev + "_device_lbl"].grid(row=0, column=0, sticky="EW", columnspan = 2)
        #sensorwidget[dev + "_device_val"] = tk.Label(sensorframe[dev], bg="white", font=medfont, text=dev)
        #sensorwidget[dev + "_device_val"].grid(row=0, column=1, sticky="EW")
        self.sensorcontainer.window_create("end", window=self.sensorframe[dev])
        
    def addwidget(self,dev,field):
        if debug:
            debugprint("Add Field:" + dev + " Field: " + field)
        key = dev + "_" + field
        if not key + "_lbl" in self.sensorwidget.keys():
            r = sensor[dev]['count']
            self.sensorwidget[key + "_lbl"] = tk.Label(self.sensorframe[dev], font=self.medfont, justify="left", text = field)
            self.sensorwidget[key + "_lbl"].grid(row = r, column = 0, sticky="W")
            self.sensorwidget[key + "_val"] = tk.Label(self.sensorframe[dev], font=self.medfont, text = sensor[dev][field])
            self.sensorwidget[key + "_val"].grid(row = r, column = 1, sticky="EW")

    def updatewidget(self,dev,field):
        if debug:
            debugprint("Update Widget:" + dev + " Field: " + field)
        key = dev + "_" + field + "_val"
        self.sensorwidget[key].config(text = sensor[dev][field])
        
    def reconfigureLayout(self,dev):
        if sensor[dev]['type'] in "ThermoBeacon,Govee H5075":
            sensor[dev]["layouttype"] = "thermo"
            debugprint("Reconfigure " + sensor[dev]['type'] + ": " + dev + " => Thermo")
            self.valueframe = tk.Frame(self.sensorframe[dev], padx=2, pady=2)
            self.valueframe.grid(row=1, column=0, columnspan=2, rowspan=2, sticky="EW")
            
            self.sensorwidget[dev+"_fullname_val"].config(bg="white", fg="black")
            self.sensorwidget[dev+"_fullname_val"].grid(row=0, column=0, columnspan=2, sticky="EW")
            self.sensorwidget[dev+"_fullname_lbl"].config(text = "Sensor ID")
            self.sensorwidget[dev+"_device_lbl"].config(text = dev, bg="white smoke", fg="black")
            self.sensorwidget[dev+"_device_lbl"].grid(row=8, column=1, columnspan=1)
            
            self.sensorwidget[dev+"_temp_lbl"].destroy()
            self.sensorwidget[dev+"_temp_val"].destroy()
            self.sensorwidget[dev+"_hum_lbl"].destroy()
            self.sensorwidget[dev+"_hum_val"].destroy()
        
            self.sensorwidget[dev+"_temp_val"] = tk.Label(self.valueframe, fg="dark orange", 
                                                    font=self.valfont, text = sensor[dev]["temp"])
            self.sensorwidget[dev+"_temp_val"].pack(side="left")
            self.sensorwidget[dev+"_temp_lbl"] = tk.Label(self.valueframe, fg="dark orange", 
                                                    font=self.valfont, text = "°C ")
            self.sensorwidget[dev+"_temp_lbl"].pack(side="left")
            
            self.sensorwidget[dev+"_hum_lbl"] = tk.Label(self.valueframe, fg="DeepSkyBlue3", 
                                                    font=self.valfont, text = "%")
            self.sensorwidget[dev+"_hum_lbl"].pack(side="right")
            self.sensorwidget[dev+"_hum_val"] = tk.Label(self.valueframe, fg="DeepSkyBlue3", 
                                                    font=self.valfont, text = sensor[dev]["hum"])
            self.sensorwidget[dev+"_hum_val"].pack(side="right")

    def debuglog_append(self,msg):
        self.debuglog.insert('end', msg + '\n')
        self.debuglog.see('end')

    def mqttlog_append(self,msg):
        self.mqttlog.insert('end', msg)
        self.mqttlog.see('end')
    
    def addDevice(self,newdev):
        print("Add Device...\n")
        self.cbDevices['values'] = devices
        self.cbDevices.set(newdev)

    def getDevice(self) -> str:
        return self.cbDevices.get()
    
    def checkNewWidget(self,dev,field):
        key = dev + "_" + field
        if not key + "_lbl" in self.sensorwidget.keys():
            sensor[dev]['count'] = sensor[dev]['count'] + 1
            self.addwidget(dev,field)
        else:
            self.updatewidget(dev,field)
            if field=="type" and not "layouttype" in sensor[dev].keys():
                self.reconfigureLayout(dev)


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

def debugprint(msg):
        now = datetime.datetime.now()
        msg = now.strftime("%Y.%m.%d %H:%M:%S.%f") + ": " + msg
        #if isOpen:
        try:
            if 'normal' == window.state():
                #app.debuglog_append(msg)
                window.after(0,lambda: app.debuglog_append(msg))
            pass
        except:
            print("Debuglog dows not exist!")
        
        print(msg)

def addDeviceProxy(newdev):
    print("addDeviceProxy...")
    #app.addDevice(newdev)
    window.after(0, lambda: app.addDevice(newdev))

def mqttlogAppendProxy(msg):
    app.mqttlog_append(msg)
    #window.after(0,lambda: app.mqttlog_append(msg))

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
        debugprint("message topic=" + topic)    
        debugprint("message received " + payload)
        debugprint("message qos=" + str(message.qos))
        debugprint("message retain flag=" + str(message.retain))
    
    if message.retain == 0:
        if '/openhab/debug/ble2mqtt-' in topic:
            debugprint(topic.split('/')[3] + ":" + payload)
        else:    
            now = datetime.datetime.now()
            msg = now.strftime("%Y.%m.%d %H:%M:%S.%f") + ": " + topic + ':' + payload + '\n'
            mqttlogAppendProxy(msg)
            #window.after(0,lambda: app.mqttlog_append(msg))

        
    if message.retain == 1 and '/openhab/debug/ble2mqtt-' in topic:
        debugprint("Found Device: " + topic.split('/')[3])
        newdev = topic.split('/')[3]
        devices.append(newdev)
        window.after(0, lambda: app.addDevice(newdev))
        #addDeviceProxy(newdev)
    
    m = re.match("^\\/openhab\\/in\\/([0-9,a-f]{12})_(.+)\\/state",topic)
    if message.retain == 0 and m:
        (dev,field)  = m.groups()
        if not dev in sensor.keys():
            debugprint("Found Sensor: " + dev)
            sensor[dev] = {'count': 0}
            #addSensorProxy(dev)
            window.after(0,lambda: app.addsensor(dev))
                    
        sensor[dev][field] = payload
        app.checkNewWidget(dev,field)
        
    
def publishcmd(command):
    dev = app.getDevice()
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

def bblankScreenCallback():
    publishcmd("blankScreen")
    
def bdebugCallback():
    publishcmd("debug")

def on_connect(client, userdata, flags, rc, properties):
    # For paho-mqtt 2.0.0, you need to add the properties parameter.
    # def on_connect(client, userdata, flags, rc, properties):
        if rc == 0:
            debugprint("Connected to MQTT Broker!")
            debugprint("Wait for window...")
            #while('normal' != window.state()):
            #      pass
            debugprint("Subscribe...")
            client.subscribe([(conftopic,0),(debugtopic,0),(basetopic,0)])
        else:
            debugprint("Failed to connect, return code %d\n", rc)

def on_disconnect(client, userdata, rc):
    debugprint("Disconnected with result code:" + rc)
    reconnect_count, reconnect_delay = 0, FIRST_RECONNECT_DELAY
    while reconnect_count < MAX_RECONNECT_COUNT:
        debugprint("Reconnecting in " + reconnect_delay + " seconds...")
        time.sleep(reconnect_delay)

        try:
            client.reconnect()
            debugprint("Reconnected successfully!")
            return
        except Exception as err:
            debugprint(err + ". Reconnect failed. Retrying...")

        reconnect_delay *= RECONNECT_RATE
        reconnect_delay = min(reconnect_delay, MAX_RECONNECT_DELAY)
        reconnect_count += 1
    debugprint("Reconnect failed after "  + reconnect_count + " attempts. Exiting...")

def on_subscribe(client, userdata, mid, reason_code_list, properties):
    # Since we subscribed only for a single channel, reason_code_list contains
    # a single entry
    if reason_code_list[0].is_failure:
        debugprint(f"Broker rejected you subscription: {reason_code_list[0]}")
    else:
        debugprint(f"Broker granted the following QoS: {reason_code_list[0].value}")

def bConnectCallback():
    debugprint("Start Loop...")
    mqclient.loop_start()
    isOpen = True
    #bconnect.pack_forget()
    #conntab.pack_forget()

# ************************* Main ****************************

ssid = getWiFi()
print("SSID:", ssid)

if "UPC4E87B2D" in ssid:
    mqttBroker ="192.168.20.17"
else:
    mqttBroker ="82.165.176.152"

#window = tk.Tk()

window.title("BLE2MQTT GUI")
app = App(window)

mqclient = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,"DesktopGUI")
mqclient.on_connect=on_connect
mqclient.on_disconnect=on_disconnect
mqclient.on_message=on_message
mqclient.on_subscribe=on_subscribe
mqclient.connect(mqttBroker)
#mqttlogAppendProxy("Test...")
debugprint("Start Loop...")
mqclient.loop_start()

debugprint("Open Window...")
window.mainloop()

isOpen = False
mqclient.loop_stop()
debugprint("Stop Loop...")

