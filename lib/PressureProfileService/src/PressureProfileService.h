#ifndef PressureProfileService_h
#define PressureProfileService_h
#include <Configuration.h>

struct PressureProfile
{
    int profileId;
    int pressure[60];
    int shotLength;
};

class PressureProfileService
{
public:
    PressureProfileService(Configuration *configuration);
    bool updatePressureProfile(int profileId, int pressure[60]);
    PressureProfile getPressureProfileById(int profileId);
    PressureProfile getLoadedPressureProfile();

private:
    PressureProfile _pressureProfile;
};

#endif
