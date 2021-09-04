#!/usr/bin/env python3
from dataclasses import dataclass
from smbus import SMBus
from sys import argv
import numpy
import logging
import time

COMMAND_RESET = 0x06
COMMAND_START = 0x08
COMMAND_READ_DATA = 0x10
COMMAND_READ_CONFIG_REG = 0x20
COMMAND_READ_STATUS_REG = 0x24
COMMAND_WRITE_CONFIG = 0x40

STATUS_REGISTER_READY = 0B10000000

# Config register can be used to define which channel to read, single vs. continuous and sampling
# rate and internal gain, but default values of A0-A1, single shot at lowest speed is fine.
CONFIG_REGISTER_VALUE = 0x00

INTERNAL_REFERENCE_VOLTAGE = 2.048 
RAW_VALUE_RANGE = 0x8000 

logger = logging.getLogger(__name__)

@dataclass
class Result:
    voltage: float
    raw_value: int

def read_voltage(bus_number, address):
    i2cbus = SMBus(bus_number)

    i2cbus.write_byte(address, COMMAND_RESET)
    i2cbus.write_byte_data(address, COMMAND_WRITE_CONFIG, CONFIG_REGISTER_VALUE)
    i2cbus.write_byte(address, COMMAND_START)

    while (True):
        # status bit 7 represents whether a sample is ready
        status = i2cbus.read_byte_data(address, COMMAND_READ_STATUS_REG)
        if status & STATUS_REGISTER_READY != 0:
            break
        time.sleep(0.010)

    # ADS1119 data value is returned MSB first, opposite SMB bus protocol
    # need to keep it as signed int16 until byte swapping is complete so the
    # sign can be extracted properly. Return it as a regular python int to allow
    # later serialization
    raw_value = numpy.int16(i2cbus.read_word_data(address, COMMAND_READ_DATA))
    raw_value = numpy.int16(((raw_value & 0x00FF) << 8) | ((raw_value >> 8) & 0x00FF))
    raw_value = int(raw_value)

    voltage = INTERNAL_REFERENCE_VOLTAGE * raw_value / RAW_VALUE_RANGE 
    logger.info(f"Read voltage={voltage:.3f}V, raw_value=0x{raw_value & 0xFFFF:04X}")
    return Result(voltage, raw_value)

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    if (len(argv) != 3):
        print(f"Usage: {argv[0]} bus_number address")
    else:
        read_voltage(int(argv[1], 0), int(argv[2], 0))
