/*
    10micron INDI driver
    GM1000HPS GM2000QCI GM2000HPS GM3000HPS GM4000QCI GM4000HPS AZ2000
    Mount Command Protocol 2.14.11

    Copyright (C) 2017 Hans Lambermont

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "lx200_10micron.h"

#include "indicom.h"
#include "lx200driver.h"

#include <cstring>
#include <termios.h>
#include <math.h>

#define PRODUCT_TAB   "Product"
#define ALIGNMENT_TAB   "Alignment"
#define LX200_TIMEOUT 5 /* FD timeout in seconds */

LX200_10MICRON::LX200_10MICRON() : LX200Generic()
{
    setLX200Capability(LX200_HAS_TRACKING_FREQ);
    setVersion(1, 0);
}

// Called by INDI::DefaultDevice::ISGetProperties
// Note that getDriverName calls ::getDefaultName which returns LX200 Generic
const char *LX200_10MICRON::getDefaultName()
{
    return (const char *)"10micron";
}

// Called by either TCP Connect or Serial Port Connect
bool LX200_10MICRON::Handshake()
{
    fd = PortFD;

    if (isSimulation() == true)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Simulate Connect.");
        return true;
    }

    // Set Ultra Precision Mode #:U2# , replies like 15:58:19.49 instead of 15:21.2
    DEBUG(INDI::Logger::DBG_SESSION, "Setting Ultra Precision Mode.");
    if (setCommandInt(fd, 2, "#:U") < 0)
    {
        return false;
    }

    return true;
}

// Called by ISGetProperties to initialize basic properties that are required all the time
bool LX200_10MICRON::initProperties()
{
    const bool result = LX200Generic::initProperties();

    // TODO initialize properties additional to INDI::Telescope

    IUFillNumber(&RefractionModelTemperatureN[0], "TEMPERATURE", "Celsius", "%+6.1f", -999.9, 999.9, 0, 0.);
    IUFillNumberVector(&RefractionModelTemperatureNP, RefractionModelTemperatureN, 1, getDeviceName(),
        "REFRACTION_MODEL_TEMPERATURE", "Temperature", ALIGNMENT_TAB, IP_RW, 60, IPS_IDLE);

    IUFillNumber(&RefractionModelPressureN[0], "PRESSURE", "hPa", "%6.1f", 0.0, 9999.9, 0, 0.);
    IUFillNumberVector(&RefractionModelPressureNP, RefractionModelPressureN, 1, getDeviceName(),
        "REFRACTION_MODEL_PRESSURE", "Pressure", ALIGNMENT_TAB, IP_RW, 60, IPS_IDLE);

    IUFillNumber(&ModelCountN[0], "COUNT", "#", "%.0f", 0, 999, 0, 0);
    IUFillNumberVector(&ModelCountNP, ModelCountN, 1, getDeviceName(),
        "MODEL_COUNT", "Models", ALIGNMENT_TAB, IP_RO, 60, IPS_IDLE);

    IUFillNumber(&AlignmentStarsN[0], "COUNT", "#", "%.0f", 0, 100, 0, 0);
    IUFillNumberVector(&AlignmentStarsNP, AlignmentStarsN, 1, getDeviceName(),
        "ALIGNMENT_STARS", "Alignment stars", ALIGNMENT_TAB, IP_RO, 60, IPS_IDLE);

    return result;
}

// this should move to some generic library
int LX200_10MICRON::monthToNumber(const char *monthName)
{
    struct entry
    {
        const char *name;
        int id;
    };
    entry month_table[] = { { "Jan", 1 },  { "Feb", 2 },  { "Mar", 3 },  { "Apr", 4 }, { "May", 5 },
                            { "Jun", 6 },  { "Jul", 7 },  { "Aug", 8 },  { "Sep", 9 }, { "Oct", 10 },
                            { "Nov", 11 }, { "Dec", 12 }, { nullptr, 0 } };
    entry *p            = month_table;
    while (p->name != nullptr)
    {
        if (strcasecmp(p->name, monthName) == 0)
            return p->id;
        ++p;
    }
    return 0;
}

// Called by INDI::Telescope when connected state changes to add/remove properties
bool LX200_10MICRON::updateProperties()
{
    bool result = LX200Generic::updateProperties();

    if (isConnected())
    {
        defineNumber(&RefractionModelTemperatureNP);
        defineNumber(&RefractionModelPressureNP);
        defineNumber(&ModelCountNP);
        defineNumber(&AlignmentStarsNP);

        getBasicData();
    }
    else
    {
        // delete properties from getBasicData
        deleteProperty(ProductTP.name);

        deleteProperty(RefractionModelTemperatureNP.name);
        deleteProperty(RefractionModelPressureNP.name);
        deleteProperty(ModelCountNP.name);
        deleteProperty(AlignmentStarsNP.name);

        // TODO delete new'ed stuff from getBasicData
    }
    return result;
}

// INDI::Telescope calls ReadScopeStatus() every updatePeriodMS to check the link to the telescope and update its state and position.
// The child class should call newRaDec() whenever a new value is read from the telescope.
bool LX200_10MICRON::ReadScopeStatus()
{
    if (!isConnected())
    {
        return false;
    }
    if (isSimulation())
    {
        mountSim();
        return true;
    }

    // Read scope status, based loosely on LX200_GENERIC::getCommandString
    char cmd[] = "#:Ginfo#";
    char data[80];
    char *term;
    int error_type;
    int nbytes_write = 0, nbytes_read = 0;
    // DEBUGFDEVICE(getDefaultName(), DBG_SCOPE, "CMD <%s>", cmd);
    if ((error_type = tty_write_string(fd, cmd, &nbytes_write)) != TTY_OK)
    {
        return false;
    }
    error_type = tty_read_section(fd, data, '#', LX200_TIMEOUT, &nbytes_read);
    tcflush(fd, TCIFLUSH);
    if (error_type != TTY_OK)
    {
        return false;
    }
    term = strchr(data, '#');
    if (term)
    {
        *(term + 1) = '\0';
    }
    else
    {
        return false;
    }
    DEBUGFDEVICE(getDefaultName(), DBG_SCOPE, "CMD <%s> RES <%s>", cmd, data);

    // Now parse the data
    float RA_JNOW   = 0.0;
    float DEC_JNOW  = 0.0;
    char SideOfPier = 'x';
    float AZ        = 0.0;
    float ALT       = 0.0;
    float Jdate     = 0.0;
    int Gstat       = -1;
    int SlewStatus  = -1;
    nbytes_read = sscanf(data, "%g,%g,%c,%g,%g,%g,%d,%d#", &RA_JNOW, &DEC_JNOW, &SideOfPier, &AZ, &ALT, &Jdate, &Gstat,
                         &SlewStatus);
    if (nbytes_read < 0)
    {
        return false;
    }

    if (Gstat != OldGstat && OldGstat != -1)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Gstat changed from %d to %d", OldGstat, Gstat);
    }
    switch (Gstat)
    {
        case GSTAT_TRACKING:
            TrackState = SCOPE_TRACKING;
            break;
        case GSTAT_STOPPED:
            TrackState = SCOPE_IDLE;
            break;
        case GSTAT_PARKING:
            TrackState = SCOPE_PARKING;
            break;
        case GSTAT_UNPARKING:
            TrackState = SCOPE_TRACKING;
            break;
        case GSTAT_SLEWING_TO_HOME:
            TrackState = SCOPE_SLEWING;
            break;
        case GSTAT_PARKED:
            TrackState = SCOPE_PARKED;
            if (!isParked())
                SetParked(true);
            break;
        case GSTAT_SLEWING_OR_STOPPING:
            TrackState = SCOPE_SLEWING;
            break;
        case GSTAT_NOT_TRACKING_AND_NOT_MOVING:
            TrackState = SCOPE_IDLE;
            break;
        case GSTAT_MOTORS_TOO_COLD:
            TrackState = SCOPE_IDLE;
            break;
        case GSTAT_TRACKING_OUTSIDE_LIMITS:
            TrackState = SCOPE_TRACKING;
            break;
        case GSTAT_FOLLOWING_SATELLITE:
            TrackState = SCOPE_TRACKING;
            break;
        case GSTAT_NEED_USEROK:
            TrackState = SCOPE_IDLE;
            break;
        case GSTAT_UNKNOWN_STATUS:
            TrackState = SCOPE_IDLE;
            break;
        case GSTAT_ERROR:
            TrackState = SCOPE_IDLE;
            break;
        default:
            return false;
    }

    OldGstat = Gstat;
    NewRaDec(RA_JNOW, DEC_JNOW);
    return true;
}

// Called by updateProperties
void LX200_10MICRON::getBasicData()
{
    DEBUGFDEVICE(getDefaultName(), DBG_SCOPE, "<%s>", __FUNCTION__);

    // cannot call LX200Generic::getBasicData(); as getTimeFormat :Gc# and getSiteName :GM# are not implemented on 10Micron
    // TODO delete SiteNameT SiteNameTP
    if (!isSimulation())
    {
        getAlignment();
        checkLX200Format(fd);
        timeFormat = LX200_24;

        if (getTrackFreq(PortFD, &TrackFreqN[0].value) < 0)
            DEBUG(INDI::Logger::DBG_WARNING, "Failed to get tracking frequency from device.");
        else
            IDSetNumber(&TrackingFreqNP, nullptr);

        char RefractionModelTemperature[80];
        getCommandString(PortFD, RefractionModelTemperature, "#:GRTMP#");
        float rmtemp;
        sscanf(RefractionModelTemperature, "%f#", &rmtemp);
        RefractionModelTemperatureN[0].value = (double) rmtemp;
        DEBUGF(INDI::Logger::DBG_SESSION, "RefractionModelTemperature read to be %0+6.1f degrees C", RefractionModelTemperatureN[0].value);
        IDSetNumber(&RefractionModelTemperatureNP, nullptr);

        char RefractionModelPressure[80];
        getCommandString(PortFD, RefractionModelPressure, "#:GRPRS#");
        float rmpres;
        sscanf(RefractionModelPressure, "%f#", &rmpres);
        RefractionModelPressureN[0].value = (double) rmpres;
        DEBUGF(INDI::Logger::DBG_SESSION, "RefractionModelPressure read to be %06.1f hPa", RefractionModelPressureN[0].value);
        IDSetNumber(&RefractionModelPressureNP, nullptr);

        int ModelCount;
        getCommandInt(PortFD, &ModelCount, "#:modelcnt#");
        ModelCountN[0].value = (double) ModelCount;
        DEBUGF(INDI::Logger::DBG_SESSION, "%d Alignment Models", (int) ModelCountN[0].value);
        IDSetNumber(&ModelCountNP, nullptr);

        int AlignmentStars;
        getCommandInt(PortFD, &AlignmentStars, "#:getalst#");
        AlignmentStarsN[0].value = (double) AlignmentStars;
        DEBUGF(INDI::Logger::DBG_SESSION, "%d Alignment Stars", (int) AlignmentStarsN[0].value);
        IDSetNumber(&AlignmentStarsNP, nullptr);

        getMountInfo();
    }
    sendScopeLocation();
    sendScopeTime();
}

// Called by getBasicData
bool LX200_10MICRON::getMountInfo()
{
    DEBUG(INDI::Logger::DBG_SESSION, "Getting product info.");
    char ProductName[80];
    getCommandString(PortFD, ProductName, "#:GVP#");
    char ControlBox[80];
    getCommandString(PortFD, ControlBox, "#:GVZ#");
    char FirmwareVersion[80];
    getCommandString(PortFD, FirmwareVersion, "#:GVN#");
    char FirmwareDate1[80];
    getCommandString(PortFD, FirmwareDate1, "#:GVD#");
    char FirmwareDate2[80];
    char mon[4];
    int dd, yyyy;
    sscanf(FirmwareDate1, "%s %02d %04d", mon, &dd, &yyyy);
    getCommandString(PortFD, FirmwareDate2, "#:GVT#");
    char FirmwareDate[80];
    snprintf(FirmwareDate, 80, "%04d-%02d-%02dT%s", yyyy, monthToNumber(mon), dd, FirmwareDate2);

    IUFillText(&ProductT[0], "NAME", "Product Name", ProductName);
    IUFillText(&ProductT[1], "CONTROL_BOX", "Control Box", ControlBox);
    IUFillText(&ProductT[2], "FIRMWARE_VERSION", "Firmware Version", FirmwareVersion);
    IUFillText(&ProductT[3], "FIRMWARE_DATE", "Firmware Date", FirmwareDate);
    IUFillTextVector(&ProductTP, ProductT, 4, getDeviceName(), "PRODUCT_INFO", "Product", PRODUCT_TAB, IP_RO, 60,
                     IPS_IDLE);

    defineText(&ProductTP);

    return true;
}

// this should move to some generic library
int LX200_10MICRON::setStandardProcedureWithoutRead(int fd, const char *data)
{
    int error_type;
    int nbytes_write = 0;

    DEBUGFDEVICE(getDefaultName(), DBG_SCOPE, "CMD <%s>", data);
    if ((error_type = tty_write_string(fd, data, &nbytes_write)) != TTY_OK)
    {
        return error_type;
    }
    tcflush(fd, TCIFLUSH);
    return 0;
}

bool LX200_10MICRON::Park()
{
    DEBUG(INDI::Logger::DBG_SESSION, "Parking.");
    if (setStandardProcedureWithoutRead(fd, "#:KA#") < 0)
    {
        return false;
    }
    return true;
}

bool LX200_10MICRON::UnPark()
{
    DEBUG(INDI::Logger::DBG_SESSION, "Unparking.");
    if (setStandardProcedureWithoutRead(fd, "#:PO#") < 0)
    {
        return false;
    }
    SetParked(false);
    return true;
}

bool LX200_10MICRON::SyncConfigBehaviour(bool cmcfg)
{
    DEBUG(INDI::Logger::DBG_SESSION, "SyncConfig.");
    if (setCommandInt(fd, cmcfg, "#:CMCFG") < 0)
    {
        return false;
    }
    return true;
}

int LX200_10MICRON::SetRefractionModelTemperature(double temperature)
{
    char data[16];
    snprintf(data, 16, "#:SRTMP%0+6.1f#", temperature);
    return setStandardProcedure(fd, data);
}

int LX200_10MICRON::SetRefractionModelPressure(double pressure)
{
    char data[16];
    snprintf(data, 16, "#:SRPRS%06.1f#", pressure);
    return setStandardProcedure(fd, data);
}

bool LX200_10MICRON::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (strcmp(name, "REFRACTION_MODEL_TEMPERATURE") == 0)
        {
            IUUpdateNumber(&RefractionModelTemperatureNP, values, names, n);
            if (0 != SetRefractionModelTemperature(RefractionModelTemperatureN[0].value))
            {
                DEBUG(INDI::Logger::DBG_ERROR, "SetRefractionModelTemperature error");
                RefractionModelTemperatureNP.s = IPS_ALERT;
                IDSetNumber(&RefractionModelTemperatureNP, nullptr);
                return false;
            }
            RefractionModelTemperatureNP.s = IPS_OK;
            IDSetNumber(&RefractionModelTemperatureNP, nullptr);
            DEBUGF(INDI::Logger::DBG_SESSION, "RefractionModelTemperature set to %0+6.1f degrees C", RefractionModelTemperatureN[0].value);
            return true;
        }
        if (strcmp(name, "REFRACTION_MODEL_PRESSURE") == 0)
        {
            IUUpdateNumber(&RefractionModelPressureNP, values, names, n);
            if (0 != SetRefractionModelPressure(RefractionModelPressureN[0].value))
            {
                DEBUG(INDI::Logger::DBG_ERROR, "SetRefractionModelPressure error");
                RefractionModelPressureNP.s = IPS_ALERT;
                IDSetNumber(&RefractionModelPressureNP, nullptr);
                return false;
            }
            RefractionModelPressureNP.s = IPS_OK;
            IDSetNumber(&RefractionModelPressureNP, nullptr);
            DEBUGF(INDI::Logger::DBG_SESSION, "RefractionModelPressure set to %06.1f hPa", RefractionModelPressureN[0].value);
            return true;
        }
        /*
        if (strcmp(name, "MODEL_COUNT") == 0)
        {
            IUUpdateNumber(&ModelCountNP, values, names, n);
            IDSetNumber(&ModelCountNP, nullptr);
            DEBUGF(INDI::Logger::DBG_SESSION, "ModelCount %d", ModelCountN[0].value);
            return true;
        }
        */
    }

    // Let INDI::LX200Generic handle any other number properties
    return LX200Generic::ISNewNumber(dev, name, values, names, n);
}
