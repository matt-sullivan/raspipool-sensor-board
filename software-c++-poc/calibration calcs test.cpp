#include <iostream>
#include <unistd.h>
#include <stdexcept>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <limits>
#include <stdexcept>
#include <array>
#include <sys/time.h>
#include <sstream>
#include <iomanip>
using namespace std;

string asHex(uint8_t value)
{
    stringstream ss;
    ss << "0x" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)value;
    return ss.str(); 
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

int main(int argc, char** argv)
{
    try
    {
        double calibrationPHLowPH = 4.0;
        double calibrationPHLowVolts = 0.170;
        double calibrationPHHighPH = 9.2;
        double calibrationPHHighVolts = -0.129;
        double voltsPerPHTheoretical = -0.0592;
        double voltsPerPHCalibration = (calibrationPHHighVolts - calibrationPHLowVolts)
            / (calibrationPHHighPH - calibrationPHLowPH);
        double phZeroCalibration = (7.0 - calibrationPHLowPH) * voltsPerPHCalibration + calibrationPHLowVolts;

        cout << voltsPerPHCalibration << ", " << phZeroCalibration << endl;

        double voltageChannel1 = -0.071;

        double ph = 7.0 + (voltageChannel1 - phZeroCalibration) / voltsPerPHCalibration;
        double phWithoutCal = 7.0 + voltageChannel1 / voltsPerPHTheoretical;

        cout << "ORP=" << asVoltage(0.0) << ", PH=" << asPH(ph) << ", ("
            << asVoltage(voltageChannel1) << "V, " << asPH(phWithoutCal) << " without calibration)"
            << endl;

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
