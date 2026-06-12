import paho.mqtt.client as mqtt

espfilepath: str  ="./src/"

def on_subscribe(client, userdata, mid, reason_code_list, properties) -> None:
    # Since we subscribed only for a single channel, reason_code_list contains
    # a single entry
    if reason_code_list[0].is_failure:
        print(f"Broker rejected you subscription: {reason_code_list[0]}")
    else:
        print(f"Broker granted the following QoS: {reason_code_list[0].value}")

def on_unsubscribe(client, userdata, mid, reason_code_list, properties) -> None:
    # Be careful, the reason_code_list is only present in MQTTv5.
    # In MQTTv3 it will always be empty
    if len(reason_code_list) == 0 or not reason_code_list[0].is_failure:
        print("unsubscribe succeeded (if SUBACK is received in MQTTv3 it success)")
    else:
        print(f"Broker replied with failure: {reason_code_list[0]}")
    client.disconnect()

def sendfile(client: mqtt.Client, filename: str, topic: str) -> None:
    print(f"getconfig received - send file {espfilepath + filename} to {topic}")
    try:
        with open(espfilepath + filename, 'r') as file:
            lines = file.readlines()
        for line in lines:
            print(f"    {line.strip()}")
            client.publish(topic=topic + "tst", payload=line.strip())
            
    except:
        print("File not Found!")

def on_message(client: mqtt.Client, userdata, message) -> None:
    # userdata is the structure we choose to provide, here it's a list()
    try:
        payload = str(message.payload.decode("utf-8"))
    except:
        payload = "error"
        
    try:
        topic = message.topic      
    except:
        topic="error"    
    
    print(f"Received Message: {payload}")
    
    msg: list[str] = payload.split(':')
    cmd = msg[0]
    if cmd == "getconfig" and len(msg) > 1:
        sendfile(client=client, filename="esp" + msg[1] + ".conf", topic="/openhab/configuration/" + msg[1])
    
    if cmd == "unsubscribe":
        client.unsubscribe("/openhab/configuration")

def on_connect(client, userdata, flags, reason_code, properties) -> None:
    if reason_code.is_failure:
        print(f"Failed to connect: {reason_code}. loop_forever() will retry connection")
    else:
        # we should always subscribe from on_connect callback to be sure
        # our subscribed is persisted across reconnections.
        client.subscribe("/openhab/configuration")

def main() -> None:
    mqttc = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqttc.on_connect = on_connect
    mqttc.on_message = on_message
    mqttc.on_subscribe = on_subscribe
    mqttc.on_unsubscribe = on_unsubscribe
    mqttc.connect("192.168.20.17")
    mqttc.loop_forever()

if __name__ == "__main__":
    main()

