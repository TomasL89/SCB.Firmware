#ifndef Display_h
#define Display_h

#include "Arduino.h"

class Display
{
public:
    Display(int lcdCSPin, int lcdDCPin, int lcdRstPin, int lcdBlPin);
    void drawWarmUpScreenBackgroundPage(bool cooling);
    void updateWarmingUpPage(int temperature);
    void drawReadyPage();
    void drawSteamReadyPage();
    void drawProgressPage(int second, int profileTime, int temperature, int pressure, bool screenSet);
    void drawCycleFinishedPage(int cycleCount);
    void drawEcoModePage();
    void drawEcoModePageTwo();
    void drawEspressoPage(int temperature);
    void drawSteamPage(int temperature);
    void drawEspressoPreview(int temperature, int pressure[60], int profileTime);
    void drawEspressoProgress(int second, int temperature, int pressureReading, int pressure[60], int profileTime);
    void drawHomePage(int position);
    void drawProfilePage(int subPosition);
    void drawTemperaturePage(int position, int espressoTemperature, int steamTemperature);
    void drawEditingTemperature(int position, int newTemperature, bool flash);
    void drawPumpCalibration(int targetPressure, float currentPressure, int currentPower, int sensorReading);
    void drawSimpleProgressPage(int temperature, int pressure, int currentTime, int shotTime);

    void drawDiagnostics(int targetTemperature, int currentTemperature, bool steamMode, int currentPower, int pidPoint, bool connectedToWiFi);

private:
    int _lcdCSPin;
    int _lcdDCPin;
    int _lcdRstPin;
    int _lcdBlPin;
};

#endif
