#ifndef PressureProfileService_h
#define PressureProfileService_h

struct PressureProfile
{
    int profileId;
    int pressure[60];
};

class PressureProfileService
{
public:
    PressureProfileService();
    bool updatePressureProfile(int profileId, int pressure[60]);
    PressureProfile getPressureProfile(int profileId);

private:
    PressureProfile _pressureProfile;
};

#endif
