#include "Arduino.h"
#include "Configuration.h"
#include "SPIFFS.h"
#include "ArduinoJson.h"

#define DEFAULT_BOILER_TIME_OUT 0
#define DEFAULT_BOILER_STEAM_TEMPERATURE 140
#define DEFAULT_BOILER_ESPRESSO_TEMPERATURE 94
#define DEFAULT_AUTO_FLUSH_TIME 0
#define DEFAULT_FAST_HEAT_CUT_OFF 80
#define MACHINE_SETTING_SIZE 768

Configuration::Configuration()
{
    File machineFile = SPIFFS.open("/machine.txt");
    if (!machineFile)
    {
        Serial.println("No machine config found, creating new one");
        int defaultPumpCalibration[15] = {0, 20, 30, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 100};
        if (updateConfiguration(DEFAULT_BOILER_TIME_OUT, DEFAULT_BOILER_STEAM_TEMPERATURE, DEFAULT_BOILER_ESPRESSO_TEMPERATURE, DEFAULT_AUTO_FLUSH_TIME, DEFAULT_FAST_HEAT_CUT_OFF, defaultPumpCalibration, 1.0, 1.0, 1.0, 1))
        {
            Serial.println("Created default configuration and saved it.");
        }
    }
    else
    {
        StaticJsonDocument<MACHINE_SETTING_SIZE> doc;
        DeserializationError error = deserializeJson(doc, machineFile.readString());
        if (error)
        {
            Serial.println("Error deserialising file, using default");
            int defaultPumpCalibration[15] = {0, 20, 30, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 100};
            if (updateConfiguration(DEFAULT_BOILER_TIME_OUT, DEFAULT_BOILER_STEAM_TEMPERATURE, DEFAULT_BOILER_ESPRESSO_TEMPERATURE, DEFAULT_AUTO_FLUSH_TIME, DEFAULT_FAST_HEAT_CUT_OFF, defaultPumpCalibration, 1.0, 1.0, 1.0, 1))
            {
                Serial.println("Found file but it was empty, created default configuration and saved it.");
            }
            return;
        }

        _machineConfiguration.boilerTimeOut = doc["boilerTimeout"];
        _machineConfiguration.boilerSteamTemperature = doc["boilerSteamTemperature"];
        _machineConfiguration.boilerEspressoTemperature = doc["boilerEspressoTemperature"];
        _machineConfiguration.autoFlushTime = doc["autoFlushTime"];
        _machineConfiguration.fastHeatCutOff = doc["fastHeatCutOff"];
        _machineConfiguration.boilerP = doc["boilerP"];
        _machineConfiguration.boilerI = doc["boilerI"];
        _machineConfiguration.boilerD = doc["boilerD"];
        _machineConfiguration.selectedProfile = doc["selectedProfile"];

        JsonArray pumpCalibration = doc["pumpCalibration"];

        for (int i = 0; i < 15; i++)
        {
            _machineConfiguration.pumpCalibration[i] = pumpCalibration[i];
        }
    }
}

bool Configuration::updateConfiguration(int boilerTimeOut, int boilerSteamTemperature, int boilerEspressoTemperature, int autoFlushTime, int fastHeatCutOff, int pumpCalibration[15], double boilerP, double boilerI, double boilerD, int selectedProfile)
{
    StaticJsonDocument<MACHINE_SETTING_SIZE> doc;
    doc["boilerTimeout"] = boilerTimeOut;
    doc["boilerSteamTemperature"] = boilerSteamTemperature;
    doc["boilerEspressoTemperature"] = boilerEspressoTemperature;
    doc["autoFlushTime"] = autoFlushTime;
    doc["fastHeatCutOff"] = fastHeatCutOff;
    doc["boilerP"] = boilerP;
    doc["boilerI"] = boilerI;
    doc["boilerD"] = boilerD;
    doc["selectedProfile"] = selectedProfile;

    JsonArray pumpCalibrationDoc = doc.createNestedArray("pumpCalibration");

    _machineConfiguration.boilerTimeOut = boilerTimeOut;
    _machineConfiguration.boilerSteamTemperature = boilerSteamTemperature;
    _machineConfiguration.boilerEspressoTemperature = boilerEspressoTemperature;
    _machineConfiguration.autoFlushTime = autoFlushTime;
    _machineConfiguration.fastHeatCutOff = fastHeatCutOff;
    _machineConfiguration.boilerP = boilerP;
    _machineConfiguration.boilerI = boilerI;
    _machineConfiguration.boilerD = boilerD;
    _machineConfiguration.selectedProfile = selectedProfile;

    for (int i = 0; i < 15; i++)
    {
        _machineConfiguration.pumpCalibration[i] = pumpCalibration[i];
        pumpCalibrationDoc.add(pumpCalibration[i]);
    }

    File file = SPIFFS.open("/machine.txt", FILE_WRITE);
    if (!file)
    {
        Serial.println("Unable to open machine file for writing");
        return false;
    }
    char output[MACHINE_SETTING_SIZE];
    serializeJson(doc, output);
    Serial.print(output);
    if (file.print(output))
    {
        Serial.println("File written");
    }
    else
    {
        Serial.println("File failed");
    }

    file.close();
    return true;
}

MachineConfiguration Configuration::getConfiguration()
{
    return _machineConfiguration;
}
