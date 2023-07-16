#ifndef Stats_h
#define Stats_h

class Stats {
    public:
        Stats();
        void updateCycleCount();
        int getCycleCount();
    private:
        int cycleCount;
};

#endif
