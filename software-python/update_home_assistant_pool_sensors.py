#!/usr/bin/env python3
from collections.abc import Mapping, Iterable
from decimal import Decimal
from sys import argv
import paho.mqtt.client as mqtt
import read_pool_sensors as pool_sensors
import dataclasses
import json
import logging
import config

BROKER="homeassistant.local"
UNIQUE_ID_PREFIX = "b827eb771bbc_pool_controller_"
DISCOVERY_CONTENT_DEVICE =  {
    "name": "Raspberry Pi Zero-Pool",
    "ids": ["b8:27:eb:77:1b:bc"],
    "suggested_area": "Pool"
}

logger = logging.getLogger(__name__)

def discovery_topic(sensor_name):
    return f"homeassistant/sensor/{sensor_name}/config"

def status_topic(sensor_name):
    return f"tele/{sensor_name}/status"

def discovery_message(sensor_name, value_field, unit_of_measurement, device_class):
    message = {
        "name": sensor_name,
        "state_topic": status_topic(sensor_name),
        "value_template": f"{{{{ value_json.{value_field} }}}}",
        "json_attributes_topic": status_topic(sensor_name),
        "unique_id": UNIQUE_ID_PREFIX + sensor_name,
        "dev": DISCOVERY_CONTENT_DEVICE
    }

    if (unit_of_measurement):
        message["unit_of_measurement"] = unit_of_measurement
    if (device_class):
        message["device_class"] = device_class

    return json.dumps(message)

class DecimalEncoder(json.JSONEncoder):
    def encode(self, obj):
        if isinstance(obj, Mapping):
            return '{' + ', '.join(f'{self.encode(k)}: {self.encode(v)}' for (k, v) in obj.items()) + '}'
        if isinstance(obj, Iterable) and (not isinstance(obj, str)):
            return '[' + ', '.join(map(self.encode, obj)) + ']'
        if isinstance(obj, Decimal):
            return f'{obj.normalize():f}'  # using normalize() gets rid of trailing 0s, using ':f' prevents scientific notation
        return super().encode(obj)

def init_mqtt():
    mqtt_client = mqtt.Client()
    mqtt_client.username_pw_set(config.mqtt_username, config.mqtt_password)
    mqtt_client.connect(BROKER)
    mqtt_client.loop_start()
    return mqtt_client

def update_sensor_config(mqtt_client, sensor_name, value_field, unit_of_measurement, device_class):
    topic = discovery_topic(sensor_name)
    message = discovery_message(sensor_name, value_field, unit_of_measurement, device_class)
    logger.info(f"Sending discovery topic={topic}, message={message}")
    # Retaining config messages should be useful if home assistant restarts, but the flag to the
    # following method doesn't seem to work (HA mqtt debugging still shows false.)
    message_info = mqtt_client.publish(topic, message, qos=1, retain=True)
    message_info.wait_for_publish()

def update_sensor_values(mqtt_client, sensor_name, values):
    topic = status_topic(sensor_name)
    message = json.dumps(dataclasses.asdict(values), cls=DecimalEncoder)
    logger.info(f"Sending status topic={topic}, message={message}")
    message_info = mqtt_client.publish(topic, message)
    message_info.wait_for_publish()

def update_all_sensor_configs(mqtt_client):
    update_sensor_config(mqtt_client, "pool_ph", "ph", None, None)
    update_sensor_config(mqtt_client, "pool_orp", "orp", "mV", None)
    update_sensor_config(mqtt_client, "pool_temp", "temperature", "°C", "temperature")

def update_all_sensor_values(mqtt_client):
    update_sensor_values(mqtt_client, "pool_ph",pool_sensors.read_ph())
    update_sensor_values(mqtt_client, "pool_orp", pool_sensors.read_orp())
    update_sensor_values(mqtt_client, "pool_temp", pool_sensors.read_temp())

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    mqtt_client = init_mqtt()
    if len(argv) > 1 and argv[1] == 'config' :
        update_all_sensor_configs(mqtt_client)
    elif len(argv) > 1 and argv[1] == 'values':
        update_all_sensor_values(mqtt_client)
    else:
        print(f"Usage: {argv[0]} config|values")
    mqtt_client.disconnect()
