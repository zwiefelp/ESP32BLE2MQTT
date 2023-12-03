from tkinter import ttk
import tkinter as tk
import paho.mqtt.client as mqtt
import time
import subprocess, platform

debug = False

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
        print("message topic=",topic)    
        print("message received " , payload)
        print("message qos=",message.qos)
        print("message retain flag=",message.retain)
    
    if message.retain == 0:
        textbox.insert('end', time.ctime() + ": " + topic + ':' + payload + '\n')
        textbox.see('end')
    if message.retain == 1 and '/openhab/debug/ble2mqtt-' in topic:
        newdev = topic.split('/')[3]
        devices.append(newdev)
        cbDevices['values'] = devices
        cbDevices.set(newdev)
        print("Found: " + topic.split('/')[3])
        
def publishcmd(command):
    dev = cbDevices.get()
    if dev != '':
        testtopic = "/openhab/configuration/" + dev
        if debug:
            print(testtopic + " => " + command)
        try:
            mqclient.publish(testtopic +  "/cmd", command)
        except Exception as e:
            print ("MQ send failed: {}".format(e))
    
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

textbox = tk.Text(height = 60,  width = 120)
vsb = tk.Scrollbar(orient="vertical", command = textbox.yview)
textbox.configure(yscrollcommand=vsb.set)
vsb.pack(side="right", fill="y")
textbox.pack(side="left", fill="both", expand=True)

print("Start Loop...")
mqclient.loop_start()
print("Subscribe...")
mqclient.subscribe([(conftopic,0),(debugtopic,0),(basetopic,0)])
window.mainloop()
mqclient.loop_stop()
print("Stop Loop...")
