#ifndef Api_h
#define Api_h
#include <Configuration.h>
#include <PressureProfileService.h>

class Api
{
public:
    Api(int serverPort, Configuration *configuration, PressureProfileService *pressureProfileService);
    void sendDataPayload(char payload[384]);
};

#endif