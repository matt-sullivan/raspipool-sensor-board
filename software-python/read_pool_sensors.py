#!/usr/bin/env python3
from dataclasses import dataclass
from w1thermsensor import W1ThermSensor
from sys import argv
import ads1119
import decimal
import logging

BUS_NUMBER = 1
ADDRESS_PH = 0x40
ADDRESS_ORP = 0x45
OP_AMP_GAIN_PH = 1 + 1.0e6/151.0e3;
OP_AMP_GAIN_ORP = 1 + 200.0e3/151.0e3;
VOLTS_PER_PH = -0.0592

logger = logging.getLogger(__name__)

@dataclass
class PH_Result:
    ph: decimal.Decimal
    voltage: decimal.Decimal
    raw_value: int

@dataclass
class ORP_Result:
    orp: int
    raw_value: int

@dataclass
class Temp_Result:
    temperature: decimal.Decimal

def round_to_decimal(value, decimals):
    # Using an intermediate string looks like the simplest way to control the number of decimal
    # places. All functions in the decimal class use precision which includes digits to the left
    # of the decimal.
    return decimal.Decimal(f"{value:.{decimals}f}")

def read_ph():
    adc_result = ads1119.read_voltage(BUS_NUMBER, ADDRESS_PH)
    raw_value = adc_result.raw_value
    input_voltage = adc_result.voltage / OP_AMP_GAIN_PH
    ph = 7.0 + input_voltage / VOLTS_PER_PH
    # compute all math before rounding, the decimal type doesn't support divide by float
    input_voltage = round_to_decimal(input_voltage, 3)
    ph = round_to_decimal(ph, 2)
    logger.info(f"Read PH {ph}, voltage={input_voltage}V, raw_value=0x{raw_value & 0xFFFF:04X}")
    return PH_Result(ph, input_voltage, raw_value)

def read_orp():
    adc_result = ads1119.read_voltage(BUS_NUMBER, ADDRESS_ORP)
    raw_value = adc_result.raw_value
    input_voltage = adc_result.voltage / OP_AMP_GAIN_ORP
    orp = int(input_voltage * 1000)
    logger.info(f"Read ORP {orp}mV, raw_value=0x{adc_result.raw_value & 0xFFFF:04X}")
    return ORP_Result(orp, raw_value)

def read_temp():
    sensor = W1ThermSensor()
    temp = sensor.get_temperature()
    temp = round_to_decimal(temp, 1)
    logger.info(f"Read temp {temp}")
    return Temp_Result(temp)

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    read_ph()
    read_orp()
    read_temp()
