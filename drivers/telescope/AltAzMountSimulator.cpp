/** \file AltAzMountSimulator.cpp
    \brief Construct a basic INDI telescope device that simulates an AltAz mount.
*/

#include "AltAzMountSimulator.h"

#include "indicom.h"
#include "indistandardproperty.h"

#include <libnova/transform.h>
#include <libnova/precession.h>
// libnova specifies round() on old systems and it collides with the new gcc 5.x/6.x headers
#define HAVE_ROUND
#include <libnova/utility.h>

#include <cmath>
#include <memory>
#include <cstring>
#include <assert.h>

static std::unique_ptr<AltAzMountSimulator> altAzMountSimulator(new AltAzMountSimulator());

/**************************************************************************************
** Return properties of device.
***************************************************************************************/
void ISGetProperties(const char *dev)
{
    altAzMountSimulator->ISGetProperties(dev);
}

/**************************************************************************************
** Process new switch from client
***************************************************************************************/
void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    altAzMountSimulator->ISNewSwitch(dev, name, states, names, n);
}

/**************************************************************************************
** Process new text from client
***************************************************************************************/
void ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    altAzMountSimulator->ISNewText(dev, name, texts, names, n);
}

/**************************************************************************************
** Process new number from client
***************************************************************************************/
void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    altAzMountSimulator->ISNewNumber(dev, name, values, names, n);
}

/**************************************************************************************
** Process new blob from client
***************************************************************************************/
void ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[],
               char *names[], int n)
{
    altAzMountSimulator->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);
}

/**************************************************************************************
** Process snooped property from another driver
***************************************************************************************/
void ISSnoopDevice(XMLEle *root)
{
    altAzMountSimulator->ISSnoopDevice(root);
}

AltAzMountSimulator::AltAzMountSimulator()
{
    // We add an additional debug level so we can log verbose scope status
    DBG_SCOPE = INDI::Logger::getInstance().addDebugLevel("Scope Verbose", "SCOPE");
}

/**************************************************************************************
** We init our properties here.
***************************************************************************************/
bool AltAzMountSimulator::initProperties()
{
    // ALWAYS call initProperties() of parent first
    INDI::Telescope::initProperties();

    // Add Debug control so end user can turn debugging/loggin on and off
    addDebugControl();
    addSimulationControl();

    IUFillSwitch(&GotoModeS[0], "ALTAZ", "Alt/Az", ISS_OFF);
    IUFillSwitch(&GotoModeS[1], "RADEC", "Ra/Dec", ISS_ON);
    IUFillSwitchVector(&GotoModeSP, GotoModeS, NARRAY(GotoModeS), getDeviceName(), "GOTOMODE", "Goto mode",
                       "Options", IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    registerProperty(&GotoModeSP, INDI_SWITCH);

    // Enable simulation mode so that serial connection in INDI::Telescope does not try
    // to attempt to perform a physical connection to the serial port.
    setSimulation(true);

    // Set telescope capabilities. 0 is for the the number of slew rates that we support. We have none for this simple driver.
    SetTelescopeCapability(TELESCOPE_CAN_GOTO | TELESCOPE_CAN_ABORT | TELESCOPE_HAS_TIME | TELESCOPE_HAS_LOCATION, 0);

    //////////////////////////////////////////////////////////////////////////////////////////////////
    /// Horizontal Coords
    //////////////////////////////////////////////////////////////////////////////////////////////////
    IUFillNumber(&HorizontalCoordsN[AXIS_AZ], "AZ", "Az D:M:S", "%10.6m", 0.0, 360.0, 0.0, 0);
    IUFillNumber(&HorizontalCoordsN[AXIS_ALT], "ALT", "Alt  D:M:S", "%10.6m", -90., 90.0, 0.0, 0);
    IUFillNumberVector(&HorizontalCoordsNP, HorizontalCoordsN, 2, getDeviceName(), "HORIZONTAL_COORD",
                       "Horizontal Coord", MAIN_CONTROL_TAB, IP_RW, 0, IPS_IDLE);

    return true;
}

bool AltAzMountSimulator::updateProperties()
{
    INDI::Telescope::updateProperties();

    if (isConnected())
    {

        defineNumber(&HorizontalCoordsNP);
    }
    else
    {
        deleteProperty(HorizontalCoordsNP.name);
    }

    return true;
}

bool AltAzMountSimulator::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName())) {
        ISwitchVectorProperty *svp = getSwitch(name);

        if (!strcmp(svp->name, GotoModeSP.name))
        {
            IUUpdateSwitch(svp, states, names, n);
            ISwitch *sp = IUFindOnSwitch(svp);

            assert(sp != nullptr);

            if (!strcmp(sp->name, GotoModeS[0].name))
                SetAltAzMode(true);
            else
                SetAltAzMode(false);
            return true;
        }

    }

    return INDI::Telescope::ISNewSwitch(dev, name, states, names, n);
}

/**************************************************************************************
** INDI is asking us to check communication with the device via a handshake
***************************************************************************************/
bool AltAzMountSimulator::Handshake()
{
    // When communicating with a real mount, we check here if commands are receieved
    // and acknolowedged by the mount. For AltAzMountSimulator, we simply return true.
    return true;
}

/**************************************************************************************
** INDI is asking us for our default device name
***************************************************************************************/
const char *AltAzMountSimulator::getDefaultName()
{
    return "AltAzMount simulator";
}

/**************************************************************************************
** Client is asking us to slew to a new position
***************************************************************************************/
bool AltAzMountSimulator::Goto(double ra, double dec)
{
    targetRA  = ra;
    targetDEC = dec;
    char RAStr[64]={0}, DecStr[64]={0};

    // Parse the RA/DEC into strings
    fs_sexa(RAStr, targetRA, 2, 3600);
    fs_sexa(DecStr, targetDEC, 2, 3600);

    // Mark state as slewing
    TrackState = SCOPE_SLEWING;

    // Inform client we are slewing to a new position
    LOGF_INFO("Slewing to RA: %s - DEC: %s", RAStr, DecStr);

    // Success!
    return true;
}

bool AltAzMountSimulator::SetAltAzMode(bool enable)
{
    IUResetSwitch(&GotoModeSP);

    if (enable)
    {
        ISwitch *sp = IUFindSwitch(&GotoModeSP, "ALTAZ");
        if (sp)
        {
            LOG_INFO("Using AltAz goto.");
            sp->s = ISS_ON;
        }
    }
    else
    {
        ISwitch *sp = IUFindSwitch(&GotoModeSP, "RADEC");
        if (sp)
        {
            sp->s = ISS_ON;
            LOG_INFO("Using Ra/Dec goto.");
        }
    }

    GotoModeSP.s = IPS_OK;
    IDSetSwitch(&GotoModeSP, nullptr);
    return true;
}

bool AltAzMountSimulator::MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command)
{
    INDI_UNUSED(dir);
    INDI_UNUSED(command);
    if (TrackState == SCOPE_PARKED)
    {
        LOG_ERROR("Please unpark the mount before issuing any motion commands.");
        return false;
    }

    return true;
}

bool AltAzMountSimulator::MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command)
{
    INDI_UNUSED(dir);
    INDI_UNUSED(command);
    if (TrackState == SCOPE_PARKED)
    {
        LOG_ERROR("Please unpark the mount before issuing any motion commands.");
        return false;
    }

    return true;
}

/**************************************************************************************
** Client is asking us to abort our motion
***************************************************************************************/
bool AltAzMountSimulator::Abort()
{
    return true;
}

/**************************************************************************************
** Client is asking us to report telescope status
***************************************************************************************/
bool AltAzMountSimulator::ReadScopeStatus()
{

    /* Process per current state. We check the state of EQUATORIAL_EOD_COORDS_REQUEST and act acoordingly */
    switch (TrackState)
    {
        case SCOPE_SLEWING:
            break;

        default:
            break;
    }

    char RAStr[64]={0}, DecStr[64]={0};

    // Parse the RA/DEC into strings
    fs_sexa(RAStr, currentRA, 2, 3600);
    fs_sexa(DecStr, currentDEC, 2, 3600);

    DEBUGF(DBG_SCOPE, "Current RA: %s Current DEC: %s", RAStr, DecStr);

    NewRaDec(currentRA, currentDEC);

    char Axis1Coords[MAXINDINAME] = {0}, Axis2Coords[MAXINDINAME] = {0};
//    double az  = 10; //static_cast<double>(n1) / 0x100000000 * 360.0;
//    double al  = 20; //static_cast<double>(n2) / 0x100000000 * 360.0;
    ln_equ_posn epochPos { 0, 0 };

    epochPos.ra  = currentRA * 15.0;
    epochPos.dec = currentDEC;

    struct ln_lnlat_posn lnobserver;
    struct ln_hrz_posn lnaltaz;

    lnobserver.lng = LocationN[LOCATION_LONGITUDE].value;
    if (lnobserver.lng > 180)
        lnobserver.lng -= 360;
    lnobserver.lat = LocationN[LOCATION_LATITUDE].value;
    ln_get_hrz_from_equ(&epochPos, &lnobserver, ln_get_julian_from_sys(), &lnaltaz);
    /* libnova measures azimuth from south towards west */
    double az = range360(lnaltaz.az + 180);
    double al = lnaltaz.alt;

    HorizontalCoordsN[AXIS_AZ].value = az;
    HorizontalCoordsN[AXIS_ALT].value = al;

    memset(Axis1Coords, 0, MAXINDINAME);
    memset(Axis2Coords, 0, MAXINDINAME);
    fs_sexa(Axis1Coords, az, 2, 3600);
    fs_sexa(Axis2Coords, al, 2, 3600);
    LOGF_DEBUG("AZ <%s> ALT <%s>; TrackState: %d", Axis1Coords, Axis2Coords, TrackState);

    IDSetNumber(&HorizontalCoordsNP, nullptr);

    return true;
}

void AltAzMountSimulator::TimerHit()
{
    static bool Slewing  = false;
    static bool Tracking = false;
    double dt = 0, da_ra = 0, da_dec = 0, dx = 0, dy = 0;
    int nlocked;
    static struct timeval ltv { 0, 0 };
    struct timeval tv { 0, 0 };

    // By default this method is called every POLLMS milliseconds
    /* update elapsed time since last poll, don't presume exactly POLLMS */
    gettimeofday(&tv, nullptr);

    if (ltv.tv_sec == 0 && ltv.tv_usec == 0)
        ltv = tv;

    dt  = tv.tv_sec - ltv.tv_sec + (tv.tv_usec - ltv.tv_usec) / 1e6;
    ltv = tv;

    // Calculate how much we moved since last time
    da_ra  = SLEW_RATE * dt;
    da_dec = SLEW_RATE * dt;


    // Call the base class handler
    // This normally just calls ReadScopeStatus
    INDI::Telescope::TimerHit();

    switch (TrackState)
    {
        case SCOPE_SLEWING:
            // Wait until we are "locked" into positon for both RA & DEC axis
            nlocked = 0;

            // Calculate diff in RA
            dx = targetRA - currentRA;

            // If diff is very small, i.e. smaller than how much we changed since last time, then we reached target RA.
            if (fabs(dx) * 15. <= da_ra)
            {
                currentRA = targetRA;
                nlocked++;
            }
            // Otherwise, increase RA
            else if (dx > 0)
                currentRA += da_ra / 15.;
            // Otherwise, decrease RA
            else
                currentRA -= da_ra / 15.;

            // Calculate diff in DEC
            dy = targetDEC - currentDEC;

            // If diff is very small, i.e. smaller than how much we changed since last time, then we reached target DEC.
            if (fabs(dy) <= da_dec)
            {
                currentDEC = targetDEC;
                nlocked++;
            }
            // Otherwise, increase DEC
            else if (dy > 0)
                currentDEC += da_dec;
            // Otherwise, decrease DEC
            else
                currentDEC -= da_dec;

            // Let's check if we recahed position for both RA/DEC
            if (nlocked == 2)
            {
                // Let's set state to TRACKING
                TrackState = SCOPE_TRACKING;

                LOG_INFO("Telescope slew is complete. Tracking...");
            }
            if (!Slewing)
            {
                LOG_INFO("Slewing started");
            }
            Tracking        = false;
            Slewing         = true;
        break;

    case SCOPE_TRACKING:
        if (!Tracking)
        {
            LOG_INFO("Tracking started");
        }
        Tracking = true;
        Slewing  = false;
        break;

    default:
        if (Slewing)
        {
            LOG_INFO("Slewing stopped");
        }
        if (Tracking)
        {
            LOG_INFO("Tracking stopped");
        }
        Tracking        = false;
        Slewing         = false;
        break;
    }
}
