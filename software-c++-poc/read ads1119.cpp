// Based on https://github.com/ELOWRO/ADS1119 but dependencies on Arduino 1 wire library replaced
// with linux i2c smb ioctls
#include <iostream>
#include <unistd.h>
#include <stdexcept>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
extern "C" {
  #include <i2c/smbus.h>
}
#include <sys/ioctl.h>
#include <limits>
#include <stdexcept>
#include <array>
#include <sys/time.h>
#include <sstream>
#include <iomanip>
using namespace std;

const int PollingMs = 5000;
const int ADCMaxWaitMs = 1000;
const int ADCPollingWaitUs = 1000;

const uint8_t AddressChannel1 = 0x40;
const uint8_t AddressChannel2 = 0x45;
const double OpAmpGainChannel1 = 1 + 1.0e6/151.0e3;
const double OpAmpGainChannel2 = 1 + 200.0e3/151.0e3;

#define ADS1119_RANGE ((uint16_t)32767) 
#define ADS1119_INTERNAL_REFERENCE_VOLTAGE ((float)2.048) 

#define ADS1119_MUX_P_AIN0_N_AIN1 (0B000) 
#define ADS1119_MUX_P_AIN2_N_AIN3 (0B001) 
#define ADS1119_MUX_P_AIN1_N_AIN2 (0B010) 
#define ADS1119_MUX_P_AIN0_N_AGND (0B011) 
#define ADS1119_MUX_P_AIN1_N_AGND (0B100) 
#define ADS1119_MUX_P_AIN2_N_AGND (0B101) 
#define ADS1119_MUX_P_AIN3_N_AGND (0B110) 
#define ADS1119_MUX_SHORTED_H_AVDD (0B111) 

#define ADS1119_REG_STATUS_READY (0B10000000)

enum struct ADS1119MuxConfiguration: uint8_t {
  positiveAIN0negativeAIN1 = ADS1119_MUX_P_AIN0_N_AIN1,
	positiveAIN2negativeAIN3 = ADS1119_MUX_P_AIN2_N_AIN3, 
	positiveAIN1negativeAIN2 = ADS1119_MUX_P_AIN1_N_AIN2,
	positiveAIN0negativeAGND = ADS1119_MUX_P_AIN0_N_AGND, 
	positiveAIN1negativeGND = ADS1119_MUX_P_AIN1_N_AGND, 
	positiveAIN2negativeAGND = ADS1119_MUX_P_AIN2_N_AGND, 
	positiveAIN3negativeAGND = ADS1119_MUX_P_AIN3_N_AGND,
	shortedToHalvedAVDD = ADS1119_MUX_SHORTED_H_AVDD 
};

enum struct ADS1119RegisterToRead: uint8_t {
	configuration = 0B0,
	status = 0B1
};

enum struct Gain: uint8_t {
  one = 0B0,
  four = 0B1
};

/**
	ADS1119Configuration
	@author Oktawian Chojnacki <oktawian@elowro.com>
*/
struct ADS1119Configuration
{
	enum struct DataRate: uint8_t {
		sps20 = 0B00,
		sps90 = 0B01,
		sps330 = 0B10,
		sps1000 = 0B11
	};

	enum struct ConversionMode: uint8_t {
		singleShot = 0B0,
		continuous = 0B1
	};

	enum struct VoltageReferenceSource: uint8_t {
		internal = 0B0,
		external = 0B1
	};

	// Bitmask
	ADS1119MuxConfiguration mux;

	// 0B0: Gain=1, 0B1: Gain=4
	Gain gain; 

	// 0B00: 20SP, 0B01: 90SPS, 0B10: 330SPS, 0B11: 1000SPS
	DataRate dataRate; 

	// 0B0: Single-shot conversion mode, 0B1: Continuous conversion mod
	ConversionMode conversionMode; 

	// 0B0: Internal 2.048-V reference selected (default), 0B1: External reference selected using the REFP and REFN inputs
	VoltageReferenceSource voltageReference; 

	// This is needed to convert bytes to volts
	float externalReferenceVoltage = 0;
};

enum Command: uint8_t {
    Reset = 0x06,
    Start = 0x08,
    ReadData = 0x10,
    ReadConfigReg = 0x20,
    ReadStatusReg = 0x24,
    WriteConfigReg = 0x40
};

long Elapsed(const timeval &start_time);
string asHex(uint8_t value);
string asHex(int16_t value);
string asHex(uint16_t value);
string asVoltage(double value);
string asPH(double value);

uint8_t ComputeConfig(ADS1119Configuration config)
{
    return ((uint8_t)config.mux << 5)
        | ((uint8_t)config.gain << 4);
}

void Write(int busDev, Command command, bool log)
{
    uint8_t commandByte = command;
    if (log)
        cout << "write " << asHex(commandByte) << endl;
    if (i2c_smbus_write_byte(busDev, commandByte) < 0)
        throw new runtime_error("write failed");
}

void Write(int busDev, Command command, uint8_t value, bool log)
{
    uint8_t commandByte = command;
    if (log)
        cout << "write " << asHex(commandByte) << " " << asHex(value) << endl;
    if (i2c_smbus_write_byte_data(busDev, commandByte, value) < 0)
        throw new runtime_error("write failed");
}

uint8_t ReadByte(int busDev, Command command, bool log)
{
    uint8_t commandByte = command;
    uint8_t value = i2c_smbus_read_byte_data(busDev, commandByte);
    if (log)
        cout << "read (with command " << asHex(commandByte) << ") value " << asHex(value) << endl;
    return value;
}

uint16_t ReadWord(int busDev, Command command, bool log)
{
    uint8_t commandByte = command;
    uint16_t value = i2c_smbus_read_word_data(busDev, commandByte);
    // ADS1119 is MSB first, opposite SMB bus protocol
    value = ((value & 0xff) << 8) | ((value & 0xff00) >> 8);
    if (log)
        cout << "read (with command " << asHex(commandByte) << ") value " << asHex(value) << endl;
    return value;
}

int16_t CaptureAndReadVoltage(int busDev, ADS1119Configuration config, bool logSteps, bool logRaw, bool logWait)
{
    // reset
    // i2cset -y 1 0x40 0x06 c
    if (logSteps)
        cout << "reset" << endl;
    Write(busDev, Command::Reset, logRaw);

    // set config
    // i2cset -y 1 0x40 0x40 0x00 b // diff
    if (logSteps)
        cout << "set config" << endl;
    Write(busDev, Command::WriteConfigReg, ComputeConfig(config), logRaw);
    
    // start
    //i2cset -y 1 0x40 0x08 c
    if (logSteps)
        cout << "start" << endl;
    Write(busDev, Command::Start, logRaw);

    // wait for ready
    // i2cget -y 1 0x40 0x24 b
    if (logSteps)
        cout << "waiting" << endl;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    while (true)
    {
        uint8_t status = ReadByte(busDev, Command::ReadStatusReg, logRaw && logWait);
        bool ready = (status & ADS1119_REG_STATUS_READY) != 0;
        long elapsed = Elapsed(start_time);

        if (ready)
        {
            if (logSteps)
                cout << "result available after " << elapsed << "ms" << endl;
            break;
        }
        if (elapsed > ADCMaxWaitMs)
        {
            if (logSteps)
                cout << "No result available after " << elapsed << "ms" << endl;
            return -1;
        }
        usleep(ADCPollingWaitUs);
    }
    
    // read value
    // i2cget -y 1 0x40 0x10 w
    if (logSteps)
        cout << "read value" << endl;
    return ReadWord(busDev, Command::ReadData, logRaw);
}

void CaptureChannel(int busDev, uint8_t address, double opAmpGain, bool extraReads,
    bool logSteps, bool logRaw, bool logWait)
{
    if (ioctl(busDev, I2C_SLAVE, address) < 0)
        throw new runtime_error("setting read address failed");

    ADS1119Configuration configDiff = {ADS1119MuxConfiguration::positiveAIN0negativeAIN1};
    ADS1119Configuration configDiffX4 = {ADS1119MuxConfiguration::positiveAIN0negativeAIN1, Gain::four};
    ADS1119Configuration configA0 = {ADS1119MuxConfiguration::positiveAIN0negativeAGND};
    ADS1119Configuration configA1 = {ADS1119MuxConfiguration::positiveAIN1negativeGND};
    
    double referenceVoltage = ADS1119_INTERNAL_REFERENCE_VOLTAGE;
    int16_t rawValue;
    uint8_t configByte;
    double rawVoltage, inputVoltage;
    
    rawValue = CaptureAndReadVoltage(busDev, configDiff, logSteps, logRaw, logWait);
    configByte = ReadByte(busDev, Command::ReadConfigReg, logRaw);
    rawVoltage = (rawValue * referenceVoltage) / (1 << 15);
    inputVoltage = rawVoltage / opAmpGain;
    cout << "Read Diff raw value=" << asHex(rawValue) << ", ADC=" << asVoltage(rawVoltage)
        << "V, VIn=" << asVoltage(inputVoltage)
        << "V, config=" << asHex(configByte) << endl; 

    if (extraReads)
    {
      rawValue = CaptureAndReadVoltage(busDev, configDiffX4, logSteps, logRaw, logWait);
      configByte = ReadByte(busDev, Command::ReadConfigReg, logRaw);
      rawVoltage = (rawValue * referenceVoltage) / (1 << 15) / 4;
      inputVoltage = rawVoltage / opAmpGain;
      cout << "Read Diff X4 raw value=" << asHex(rawValue) << ", ADC=" << asVoltage(rawVoltage)
          << "V, VIn=" << asVoltage(inputVoltage)
          << "V, config=" << asHex(configByte) << endl; 

      rawValue = CaptureAndReadVoltage(busDev, configA0, logSteps, logRaw, logWait);
      configByte = ReadByte(busDev, Command::ReadConfigReg, logRaw);
      rawVoltage = (rawValue * referenceVoltage) / (1 << 15);
      cout << "Read A0 raw value=" << asHex(rawValue) << ", ADC=" << asVoltage(rawVoltage)
          << "V, config=" << asHex(configByte) << endl; 

      rawValue = CaptureAndReadVoltage(busDev, configA1, logSteps, logRaw, logWait);
      configByte = ReadByte(busDev, Command::ReadConfigReg, logRaw);
      rawVoltage = (rawValue * referenceVoltage) / (1 << 15);
      cout << "Read A1 raw value=" << asHex(rawValue) << ", ADC=" << asVoltage(rawVoltage)
          << "V, config=" << asHex(configByte) << endl; 
    }
}

double CaptureAndReadVoltage(int busDev, uint8_t address, double opAmpGain,
    bool logSteps, bool logRaw, bool logWait)
{
    if (ioctl(busDev, I2C_SLAVE, address) < 0)
        throw new runtime_error("setting read address failed");

    ADS1119Configuration configDiff = {ADS1119MuxConfiguration::positiveAIN0negativeAIN1};

    double referenceVoltage = ADS1119_INTERNAL_REFERENCE_VOLTAGE;
    int16_t rawValue;
    double rawVoltage, inputVoltage;
    
    rawValue = CaptureAndReadVoltage(busDev, configDiff, logSteps, logRaw, logWait);
    rawVoltage = (rawValue * referenceVoltage) / (1 << 15);
    inputVoltage = rawVoltage / opAmpGain;
    return inputVoltage; 
}

int main(int argc, char** argv)
{
    try
    {
        double calibrationPHLowPH = 4.00;
        double calibrationPHLowVolts = 0.170;
        double calibrationPHHighPH = 9.18;
        double calibrationPHHighVolts = -0.129;
        double voltsPerPHTheoretical = -0.0592;
        double voltsPerPHCalibration = (calibrationPHHighVolts - calibrationPHLowVolts)
            / (calibrationPHHighPH - calibrationPHLowPH);
        double phZeroCalibration = (7.0 - calibrationPHLowPH) * voltsPerPHCalibration + calibrationPHLowVolts;
        
        bool debugVoltages = argc >= 2;
        bool debugChan1 = argc >= 2 && argv[0][0] != '2';

        if (!debugVoltages)
        {
            cout << "PH calibration=" << asVoltage(voltsPerPHCalibration)
                << "V/PH, " << asVoltage(phZeroCalibration) << "V at PH 7" << endl;
        }

        bool extraReads = false;
        bool logSteps = false;
        bool logRaw = false;
        bool logWait = false;

        if (logSteps)
          cout << "open" << endl;
        int busDev = open("/dev/i2c-1", O_RDWR);
        if (busDev < 0)
            throw new runtime_error("file open failed");

        while (true)
        {
            cout << endl;
            if (debugVoltages)
            {
                if (debugChan1)
                {
                  cout << "Channel 1" << endl;
                  CaptureChannel(busDev, AddressChannel1, OpAmpGainChannel1, extraReads, logSteps, logRaw, logWait);
                }
                else
                {
                  cout << "Channel 2" << endl;
                  CaptureChannel(busDev, AddressChannel2, OpAmpGainChannel2, extraReads, logSteps, logRaw, logWait);
                }
            }
            else
            {
                double voltageChannel1 = CaptureAndReadVoltage(busDev, AddressChannel1, OpAmpGainChannel1,
                    logSteps, logRaw, logWait);
                double voltageChannel2 = CaptureAndReadVoltage(busDev, AddressChannel2, OpAmpGainChannel2,
                    logSteps, logRaw, logWait);

                double ph = 7.0 + (voltageChannel1 - phZeroCalibration) / voltsPerPHCalibration;
                double phWithoutCal = 7.0 + voltageChannel1 / voltsPerPHTheoretical;

                cout << "ORP=" << asVoltage(voltageChannel2) << ", PH=" << asPH(ph) << ", ("
                    << asVoltage(voltageChannel1) << "V, " << asPH(phWithoutCal) << " without calibration)"
                    << endl;
            }
            
            usleep(PollingMs * 1000);
        }
    }
    catch(const runtime_error& e)
    {
      cerr << "runtime error" << endl;
      cerr << e.what() << endl;
    }
    catch(const exception& e)
    {
      cerr << e.what() << endl;
    }
}


long Elapsed(const timeval &start_time)
{
   struct timeval end_time;
   long milli_time, seconds, useconds;
   gettimeofday(&end_time, NULL);
   seconds = end_time.tv_sec - start_time.tv_sec; //seconds
   useconds = end_time.tv_usec - start_time.tv_usec; //milliseconds
   milli_time = ((seconds) * 1000 + useconds/1000.0);
   return milli_time;
}

string asHex(uint8_t value)
{
    stringstream ss;
    ss << "0x" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)value;
    return ss.str(); 
}

string asHex(int16_t value)
{
  return asHex((uint16_t)value);
}

string asHex(uint16_t value)
{
    stringstream ss;
    ss << "0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << (int)value;
    return ss.str(); 
}

string asVoltage(double value)
{
  stringstream ss;
  ss << fixed << setprecision(3) << value;
  return ss.str(); 
}

string asPH(double value)
{
  stringstream ss;
  ss << fixed << setprecision(2) << value;
  return ss.str(); 
}
