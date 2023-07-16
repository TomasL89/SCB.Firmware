#ifndef Configuration_h
#define Configuration_h

struct MachineConfiguration
{
    int selectedProfile;
    int boilerTimeOut;
    int boilerSteamTemperature;
    int boilerEspressoTemperature;
    int autoFlushTime;
    int fastHeatCutOff;
    int pumpCalibration[15];
    double boilerP;
    double boilerI;
    double boilerD;
};

class Configuration
{
public:
    Configuration();
    bool updateConfiguration(int boilerTimeOut, int boilerSteamTemperature, int boilerEspressoTemperature, int autoFlushTime, int fastHeatCutOff, int pumpCalibration[15], double boilerP, double boilerI, double boilerD, int selectedProfile);
    MachineConfiguration getConfiguration();

private:
    MachineConfiguration _machineConfiguration;
};

#endif
