/** \file AltAzMountSimulator.h
    \brief Construct a basic INDI telescope device that simulates an AltAz mount.
*/

#pragma once

#include "inditelescope.h"

class AltAzMountSimulator : public INDI::Telescope
{
  public:
    AltAzMountSimulator();
    virtual bool updateProperties() override;
    virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
    virtual void TimerHit() override;

  protected:
    bool Handshake() override;

    const char *getDefaultName() override;
    bool initProperties() override;

    // Telescope specific functions
    bool ReadScopeStatus() override;
    bool Goto(double, double) override;
    bool Abort() override;
    bool SetAltAzMode(bool);
    virtual bool MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command) override;
    virtual bool MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command) override;

  private:
    double currentRA {0};
    double currentDEC {90};
    double targetRA {0};
    double targetDEC {0};

    // Debug channel to write mount logs to
    // Default INDI::Logger debugging/logging channel are Message, Warn, Error, and Debug
    // Since scope information can be _very_ verbose, we create another channel SCOPE specifically
    // for extra debug logs. This way the user can turn it on/off as desired.
    uint8_t DBG_SCOPE { INDI::Logger::DBG_IGNORE };

    ISwitch GotoModeS[2];
    ISwitchVectorProperty GotoModeSP;

    // Horizontal Coords
    INumber HorizontalCoordsN[2];
    INumberVectorProperty HorizontalCoordsNP;

    // slew rate, degrees/s
    static const uint8_t SLEW_RATE = 3;
};
