/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2005.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/


//TODO: Factor the getters so that we just need to hand them the offset within the
//      channel and the factored code will hand us the 6 values.


#include "CV6533.h"
#include "VMEAddressModifier.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <XXUSBConfigurableObject.h>

#include <string>

#include <TCLInterpreter.h>
#include <TCLObject.h>

#include <stdint.h>
#include <stdio.h>
#include <tcl.h>
#include <string.h>
#include <stdlib.h>

#include <iostream>
#include <sstream>
#include <memory>
#include <system_error>

using namespace std;
using mesytec::mvlc::MVLC;
using mesytec::mvlc::VMEDataWidth;
using namespace VMEAMod;

static const uint32_t INITIAL_BASE_VALUE(0xffffffff);   // know if this has been modified.

// Register offset:'

#ifndef Const
#define Const(name) static const uint32_t name =
#endif

// Board parameters (common to all channels):

Const(BoardVmax)     0x50;
Const(BoardImax)     0x54;
Const(BoardStatus)   0x58;
Const (Firmware)      0x5c;

// Each Channels has a common format that is described later:

Const(Channels[6])   {
  0x080, 0x100, 0x180, 0x200, 0x280, 0x300
};

// Board configuration parameters:

Const(ChannelCount)   0x8100;
Const(Description)    0x8102;
Const(Model)          0x8116;
Const(SerialNo)       0x811e;
Const(VmeFirmware)    0x8120;

//  Below are the register offsets within each of the Channels array elements:

Const(Vset)      0x00;
Const(Iset)      0x04;
Const(VMon)      0x08;
Const(IMon)      0x0c;
Const(PW)        0x10;
Const(ChStatus)  0x14;
Const(TripTime)  0x18;
Const(SVMax)     0x1c;
Const(RampDown)  0x20;
Const(RampUp)    0x24;
Const(PwDown)    0x28;
Const(Polarity)  0x2c;
Const(Temp)      0x30;

// Global status register bits:

Const(Chan0Alarm)    0x0001;
Const(Chan1Alarm)    0x0002;
Const(Chan2Alarm)    0x0004;
Const(Chan3Alarm)    0x0008;
Const(Chan4Alarm)    0x0010;
Const(PwrFail)       0x0080;
Const(OverPwr)       0x0100;
Const(MaxVUncal)     0x0200;
Const(MaxIUncal)     0x0400;

// Individual channel status register bits.

Const(On)           0x0001;
Const(RampingUp)    0x0002;
Const(RampingDown)  0x0004;
Const(OverCurrent)  0x0008;
Const(OverVoltage)  0x0010;
Const(UnderVoltage) 0x0020;
Const(MaxV)         0x0040;
Const(MaxI)         0x0080;
Const(Trip)         0x0100;
Const(OverPower)    0x0200;
Const(Disabled)     0x0400;
Const(Interlocked)  0x0800;
Const(Uncalibrated) 0x1000;


//  Address modifier to use when accessing the board:

static const uint8_t amod = a32PrivData;

/*------------------------------------- Canonical methods */

/**
 * Construct the object.  For now we won't do anything
 * with the monitored registers.  We will just let those
 * populate as the update/monitor list runs.
 */
CV6533::CV6533(MVLC* controller) :
  SlowControlsDriver(controller)
{
}

/**
 * Destruction is not really needed since we are not going to be
 * destroyed for the life of the program.
 */
CV6533::~CV6533()
{
}
/*------------------ Operations for running the module -----*/



/**
 * Called to initalize the module.  This happens on every reconfigure
 * that leaves -base as not 0xffffffff.
 */
void
CV6533::reconfigure()
{

  if(getBase() != INITIAL_BASE_VALUE) {
    
    for (unsigned int i =0; i < 6; i++) {
      turnOff( i);
      setRequestVoltage( i, 0.0);
    }
    
  }
}
/**
 * For devices with write-only registers, this function
 * would set the device to a configuration that matches
 * state held in the driver.  This function serves
 * no purpose for the CAEN V6533 and thus is a no-op.
 * @return string
 * @retval OK
 */
void
CV6533::Update()
{
  
}
/**
 *  Processes set operations for the device.  See
 * the header for a description of the supported 
 * parameters.
 * @param parameter   - Name of the parameter to set.
 *                      in the parmeter set described in the
 *                      header the n trailing each paramter name
 *                      is the channel number (0-5).
 * @param value       - Value of the parameter.
 * @return string
 * @retval "OK"   - The set worked correctly.
 * @retval "ERROR - ..." Some error occured. The remainder
 *                       of the string describes
 *                       the error
 */
string
CV6533::Set(const char* parameter, const char* value)
{
  // The meaning of the value depends on the actual parameter
  // type.  What this function does is determine the
  // root parameter name and the channel number (if
  // applicable) and dispatch to the appropriate function.

  unsigned channelNumber;
  
 
  try {

    if (sscanf(parameter, "v%u", &channelNumber) == 1) {
      setRequestVoltage(channelNumber, atof(value));
    }
    else if (sscanf(parameter, "i%u",
		    &channelNumber) == 1) {
      setRequestCurrent(channelNumber, atof(value));
    }
    else if(sscanf(parameter, "on%u", &channelNumber) == 1) {
      setChannelOnOff(channelNumber, strToBool(value));
    }
    else if (sscanf(parameter, "ttrip%u", &channelNumber) == 1) {
      setTripTime(channelNumber, atof(value));
    }
    else if (sscanf(parameter, "svmax%u", &channelNumber) == 1) {
      setMaxVoltage(channelNumber, atof(value));
    }
    else if (sscanf(parameter, "rdown%u", &channelNumber) == 1) {
      setRampDownRate(channelNumber, atof(value));
    } 
    else if (sscanf(parameter, "rup%u", &channelNumber) == 1) {
      setRampUpRate(channelNumber, atof(value));
    }
    else if (sscanf(parameter, "pdownmode%u", &channelNumber) ==1 ) {
      setPowerDownMode(channelNumber, value);
    }
    else {
      stringstream smsg;
      smsg << "Unrecognized parameter : " << parameter;
      string msg(smsg.str());
      throw msg;
    }

    // If there's a non zero length list, execute it.
    
    
  }
  catch(string msg) {
    string error = "ERROR - ";
    error += msg;
    return error;
  }
  
  return string("OK");
}
/**
 * Get - this function retrieves the value of a parameter from the device.
 *       note that if a parameter is in the monitor set it will still be read from the device
 *       though the monitored data will be updated by the read that results.
 *       For readings that are per channel, we are going to save some time by 
 *       reading all values from all channels.. When data taking is in progress, this will
 *       reduce the number of times the Get needs to be invoked and hence the number
 *       of times the DAQ needs to be stopped.
 *  @param param  - Name of the paramter to get.. in the case of the per channel parameters
 *                      leave off the channel number as all channesl will be read and returned
 *                      as a Tcl List.
 * @return string
 * @retval "ERROR -..." An error occured, the remainder of the text string is a human readable
 *                      error message.
 * @retval "OK xxx"   Everything worked.  If xxx represents a single paramter (e.g. globalmaxv
 *                    this will just be a number.  If the requested parameter was a per channel value
 *                    xxx will be a correctly formatted Tcl list that contains the per channel
 *                    values of the parameter.
 */
string
CV6533::Get(const char* param)
{
  string parameter(param);

  // error handing in the utilities means tossing exceptions with the error message.
  // the individual utilities will return the correct appropriate output (not the ok)

  string ok = "OK ";
  string result;
  try {
    // Big honking if/else to deal with all the parameters... all oparameters are readable
    // and the utilities will take care of updating the member variables:

    if (parameter == "globalmaxv") {
      result = getGlobalMaxV();
    }
    else if (parameter == "globalmaxI") {
      result = getGlobalMaxI();
    }
    else if (parameter == "v") {
      result = getChannelVoltages();
    }
    else if (parameter == "i") {
      result = getChannelCurrents();
    }
    else if (parameter == "on") {
      result = getOnOffRequests();
    }
    else if (parameter == "vact") {
      result = getActualVoltages();
    }
    else if (parameter == "iact") {
      result = getActualCurrents();
    }
    else if (parameter == "status") {
      result = getChannelStatuses();
    }
    else if (parameter == "ttrip") {
      result = getTripTimes();
    }
    else if (parameter == "svmax") {
      result = getSoftwareVmax();
    }
    else if (parameter == "rdown") {
      result = getRampDownRates();
    }
    else if (parameter == "rup") {
      result = getRampUpRates();
    }
    else if (parameter == "pdownmode") {
      result = getPowerdownModes();
    }
    else if (parameter == "polarity") {
      result = getPolarities();
    }
    else if (parameter == "temp") {
      result = getTemperatures();
    }
    else {
      stringstream smsg;
      smsg << "Invalid parameter name: " << parameter;
      string msg(smsg.str());
      throw msg;
    }
  }
  catch(string msg) {
    string error = "ERROR - ";
    error       += msg;
    return error;
  }
  ok += result;
  return ok;
}

/*----------------------------------  Monitoring --------------------------*/


\
/**
 * This function is called when a client wants our most
 * recent version of monitored data.
 * We are not allowed todo any VME actions as we are
 * explicitly not synchronized with the 
 * data taking.  We will instead return the 
 * most recent copy of the monitored data.
 * @return string
 * @retval A properly formatted tcl list that consists
 *         of the following elements:
 *         - Most recent value of the global status
 *           register.
 *         - 6 element list of most recent values of
 *           the channel status registers.
 *         - 6 element list of the most recent values of the
 *           actual voltages.
 *         - 6 element list of the most recent values of
 *           the actual channel currents.
 *         - 6 element list of the most recent values of
 *           the channel temperatures.
 */
string
CV6533::getMonitor()
{
  fillMonitoredVariables();   // Read the data from the device.


  CTCLInterpreter interp;	// Captive interpeter..fmt.
  CTCLObject      objResult; 	// Top level list result.
  CTCLObject      statusList;   // List for status vals.

  objResult.Bind(interp);

  // Global status element:

  objResult += (int)m_globalStatus;

  // Channel statuses:

  statusList.Bind(interp);
  for(int i = 0; i < 6; i++) {
    statusList += (int)m_channelStatus[i];
  }
  objResult += statusList;

  //  Channel voltages:

  objResult += scaledIToString(m_voltages, 0.1);

  // Channel Currents:

  objResult += scaledIToString(m_currents, 0.05);
  
  // Temperatures:

  objResult += scaledIToString(m_temperatures, 1.0);

  // Return as string:

  string result = "OK ";
  result       += static_cast<string>(objResult);
  return result;

}

/*----------------------------- Utility functions ----------------------------- */
/**
 * Convert a string to a boolean..throws an exception if the string is not a valid bool.
 * @param value - value to convert.
 * @return bool
 * @retval true - if the string is a valid representation of true (as defined by XXUSB::CConfigurableObject).
 * @retval false - if the string is a valid representatino of false (as defined by XXUSB::CConfigurableObject).
 * @throw string if the string is not a valid bool.
 */
bool
CV6533::strToBool(string value)
{
  if (!XXUSB::CConfigurableObject::isBool("null", value, NULL)) {
    throw string(" Invalid booleanvalue");
  }
  return XXUSB::CConfigurableObject::strToBool(value);
}
/**
 * Get the base address of the module from the configuration.
 * @return uint32_t
 * @retval - Value of the -base configuration value.
 */
uint32_t
CV6533::getBase()
{
  return getConfiguration()->getUnsignedParameter("-base");
}


/**
 * Convert a floating point value to a string.
 * @param value
 * @return string
 * @retval the string representation of the value.
 */
string
CV6533::fToString(float value)
{
  char cValue[100];
  sprintf(cValue, "%f", value);
  return string(cValue);
}

/**
 * Convert an array of 6 uint16_t values to a properly formatted list of 
 * floats after applying a scale factor.
 * @param values      - Pointer to the array of uint32_t's to convert.
 * @param scaleFactor - Foating point scale factor used to convert the values to floats.
 * @return string
 * @retval Properly formatted Tcl list of scaled floats. 
 */
string
CV6533::scaledIToString(uint32_t* values, float scaleFactor)
{
  float           scaledValue;
  CTCLInterpreter interp;
  CTCLObject      objResult;
  string          stringResult;
  
  objResult.Bind(interp);
  for (int i=0; i < 6; i ++) {
    scaledValue = values[i];
    scaledValue *= scaleFactor;
    objResult += scaledValue;
  }
  stringResult = static_cast<string>(objResult);
  return stringResult;

}


/*--------------- Setting the device registers -------------------------------*/


/**
 *  turn a specified channel off 
 * 
 * @param channel - Channel to which to add the command (not range checked).
 */
void
CV6533::turnOff( unsigned int channel)
{
  m_pVme->vmeWrite(
    getBase() + Channels[channel] + PW , (uint16_t)0, amod, VMEDataWidth::D16
  );
}
/**
 * Request the requested voltage for a channel/
 * @param channel  - Channel number whose request voltage will be modified.
 * @param value    - Floating point voltage to request.
 *
 * @note Channel and value are not range checked.
 */
void
CV6533::setRequestVoltage(unsigned int channel, float value)
{
  uint16_t vrequest = (uint16_t)(value * 10.0); // Request resolution is .1 V  see manual 3.2.2.1

  m_pVme->vmeWrite(
    getBase() + Channels[channel] + Vset, vrequest, amod, VMEDataWidth::D16
  );
}
/**
 * Add a request to set the requested current max for a specified channel to a VME operation list.
 
 * @param channel - Number of the channel to modify.
 * @param value   - The micro-amp current limit.
 * 
 * @note neither value nor channel are range checked.
 */
void
CV6533::setRequestCurrent(unsigned int channel, float value)
{
  uint16_t irequest = (uint16_t)(value/0.05); // Register value.
  m_pVme->vmeWrite(
    getBase() + Channels[channel] + Iset, irequest, amod, VMEDataWidth::D32
  );
  
} 
/**
 * Turn a channel on or off
 * 
 * @param channel  - Number of the channel to set.
 * @param on       - true to turn channel on or false to turn it off.
 *
 * @note The channel number is not range checked.
 */
void
CV6533::setChannelOnOff( unsigned int channel, bool value)
{
  m_pVme->vmeWrite(
    getBase() + Channels[channel] + PW,  
		(uint16_t)(value ? 1 : 0), amod, VMEDataWidth::D16
  ); // Not going to assume anything about true/false representation.
}
/**
 * Set the trip time.  Presumably this is the amount of time
 * an out of condition state is allowed to persist
 * before the channel trips off.
 * 
 * @param channel - Which channel should be affected.
 * @param time    - Time in seconds.
 *
 */
void 
CV6533::setTripTime( unsigned int channel, float time)
{
  uint16_t timeval = (uint16_t)(time/0.1); // register value.
  m_pVme->vmeWrite(getBase() + Channels[channel] + TripTime,
		  timeval, amod, VMEDataWidth::D16);
}
/**
 * Set the maximum allowed voltage (software voltage limit)
 * for the channel.  If a voltage setting is made above
 * this limit, the setting is limited to this level.
 *
 * @param channel - Channel that should be affected.
 * @param voltage - Voltage limit (in volts).
 */
void
CV6533::setMaxVoltage(
		       unsigned int channel, float voltage)
{
  uint16_t voltval = (uint16_t)(voltage/.1);
  m_pVme->vmeWrite(getBase() + Channels[channel] + SVMax,
		  voltval, amod, VMEDataWidth::D16);
}
/**
 * Set the rate at which a channel ramps down.
 * 
 * @param channel - Channel to modify.
 * @param rate    - Rate in Volts/sec
 */
void
CV6533::setRampDownRate(
			unsigned int channel, float rate)
{
  uint16_t rateVal = (uint16_t)rate;
  m_pVme->vmeWrite(getBase() + Channels[channel] + RampDown,
		  rateVal, amod, VMEDataWidth::D16);
}
/**
 * set the rate at which a channel ramps up in voltage.
 *
 * @param channel - Channel number affected.
 * @param rate    - Ramp rate in Volts/sec.
 */
void
CV6533::setRampUpRate(
			unsigned int channel,
			float rate)
{
  uint16_t rateVal = (uint16_t)rate;
  m_pVme->vmeWrite(getBase() + Channels[channel] + RampUp, 
    rateVal, amod, VMEDataWidth::D16
		  );
}
/**
 * Set the power down mode.
 *
 * @param channel- Number of the channel to modify.
 * @param mode   - Mode to use  This can be one of two values:
 *                 - kill : power off in a hurry.
 *                 - ramp : Ramp down at the ramp down rate.
 */
void
CV6533::setPowerDownMode(
			 unsigned int channel, string mode)
{
  uint16_t modeVal;
  if (mode == "kill") {
    modeVal = 0;
  }
  else if (mode == "ramp")  {
    modeVal = 1;
  }
  else {
    throw string("Illegal mode value");
  }
  m_pVme->vmeWrite(getBase() + Channels[channel] + PwDown,
		  modeVal, amod, VMEDataWidth::D16);

}
/*--------------------------- Getting device register values ----------------------*/

/**
 * Get the value of the global Voltage max regsiter.  This register defines
 * the maximum voltage capability of the module.
 *
 * @return string
 * @retval Stringified maximum voltage in volts.
 */
string
CV6533::getGlobalMaxV()
{
  error_code status;

  uint32_t value(0);
  status = m_pVme->vmeRead(getBase() + BoardVmax, value,
			 amod, VMEDataWidth::D16);
  if (status) {
    stringstream smsg;
    smsg << "VME Read of Global max voltage failed: " << status.message();
    string msg(smsg.str());
    throw msg;
  }
  return fToString((float)value);
}
/**
 * Return the global maximum current in microamps.
 *
 * @return string
 * @retval Stringified maximum current in micro-Amps.
 */
string
CV6533::getGlobalMaxI()
{
  error_code status;
  uint32_t value;
  status = m_pVme->vmeRead(getBase() + BoardImax,
			 value, amod, VMEDataWidth::D32);
  if (status) {
    throw string("VME Read of Global max current failed");
  }
  return fToString((float)value);
}
/**
 * Retrieve the channel voltage request values. 
 * as well.
 *
 * @return string
 * @retval 6-element Tcl list containing the actual voltages.
 */
string
CV6533::getChannelVoltages()
{
  uint32_t requests[6];
  error_code  status;
  size_t   nread;


  for (int i=0; i < 6; i++) {
    status = m_pVme->vmeRead(getBase() + Channels[i] + Vset,
		   requests[i], amod, VMEDataWidth::D16);
    if(status) {
      stringstream smsg;
      smsg << "Could not read voltage from channel: " << i 
        << " : " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  return scaledIToString(requests, 0.1);
}
/**
 *  Retrieve the channel requested currents.
 *
 * @return string
 * @retval A properly formatted Tcl list of the currents in uA.
 */
string
CV6533::getChannelCurrents()
{
  uint32_t requests[6];
  error_code status;
  size_t  nread;

  
  for (int i=0; i < 6; i++) {
    status = m_pVme->vmeRead(getBase() + Channels[i] + Iset, 
      requests[i],
		   amod, VMEDataWidth::D16);
    if (status) {
      stringstream smsg;
      smsg << "Could not read current from channel " << i
        << " : " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  

  return scaledIToString(requests, 0.05);
}
/**
 * Get the on off request values.  This is a set of values that determines if a
 * channel is requested to be on or off.  
 *
 * @return string
 * @retval a properly formatted TCl list of values for each channel. 0 means off is requested
 *         1 means on is requested.
 */
string
CV6533::getOnOffRequests()
{
  uint32_t requests[6];
  size_t   nRead;
  error_code status;


  for(int i=0; i < 6; i++) {
    status = m_pVme->vmeRead(getBase() + Channels[i]  + PW,
		   requests[i], amod, VMEDataWidth::D16);

    if (status) {
      stringstream smsg;
      smsg <<"Unbale to read channel " << i << " power on/off request state : "
        << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  
  return scaledIToString(requests, 1.0);
}
/**
 * Get the actual voltages asserted on the channel by the module.
 * This will update the m_channelStatus values as well.
 * @param vme - VMUSB controller object.
 * @return string
 * @retval Tcl formatted list of output voltages in floating point V.
 * @note updates the monitored voltage though this is no longer important.
 */
string
CV6533::getActualVoltages()
{
  size_t nRead;
  error_code    status;
  uint32_t voltage;

  for (int i=0; i < 6; i++) {
    status = m_pVme->vmeRead(getBase() + Channels[i] + VMon,
		   voltage, amod, VMEDataWidth::D16);
    if (status) {
      stringstream smsg;
      smsg <<"Unable to read channel " << i << " output voltage : " 
        << status.message();
      string msg(smsg.str());
      throw msg;
    }
    m_voltages[i] = voltage;
  }
  
  
  return scaledIToString(m_voltages, 0.1);
}
/**
 * Get the current sourced by each channel.  These are actuals not limits.
 * Note that m_currents will be updated as well by this.
 *
 * @return string
 * @retval Tcl formatted list of currents in uAmp.
 */
string
CV6533::getActualCurrents()
{
  size_t nRead;
  error_code status;
  uint32_t current;

  for (int i =0; i < 6; i++) {
    status = m_pVme->vmeRead(
      getBase() + Channels[i] + IMon, current, amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read channel " << current
        <<" output current: " << status.message();
        string msg(smsg.str());
        throw msg;
    }
    m_currents[i] = current;
  }
  
  return scaledIToString(m_currents, 0.1);
}
/**
 * Get the current values of the channel status registers.
 * This will update the values of m_chanelStatus.
 *
 * @return string
 * @retval a tcl formatted list of integer encoded strings of the register values.
 *         see the manual 3.2.2.6 for a bit by bit breakdown of the register values.
 */
string
CV6533::getChannelStatuses()
{
  size_t nRead;
  error_code   status;
  uint32_t cstat;

  for (int i =0; i < 6; i++) {
    status = m_pVme->vmeRead(
      getBase() + Channels[i] + ChStatus, cstat, amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read channel " << i << " status register: "
        << status.message();
      string msg(smsg.str());
      throw msg;
    }
    m_channelStatus[i] = cstat;
  }
  
  // Use a captive interpreter to do the list formatting:

  CTCLInterpreter interp;
  CTCLObject      resultList;
  resultList.Bind(interp);

  for (int i=0; i < 6; i++) {
    resultList += (int)m_channelStatus[i];
  }
  string result = (string)(resultList);
  return result;

  
}
/**
 * Get the trip times for all of the channels. This is the amount of time
 * in seconds a limit condition can be violated on a channel before the channel is
 * turned off.
 *
 * @return string
 * @retval  A properly formatted Tcl lists whose elements are the trip times 
 *          in secondes.
 */
string
CV6533::getTripTimes()
{
  size_t            nRead;
  error_code        status;
  
  uint32_t          tripTimes[6];

  for (int i =0; i < 6; i++) {
    m_pVme->vmeRead(getBase() + Channels[i] + TripTime, tripTimes[i],
		   amod, VMEDataWidth::D16);
    if (status) {
      stringstream smsg;
      smsg << "Unable to read trip time for channel " << i
      <<  " : " << status.message();
    }
  }
  
  return scaledIToString(tripTimes, 0.1);
}
/**
 * Get the software VMax values from all the channels.
 *
 * @return string
 * @retval Properly formatted Tcl list that contains the 
 *         software voltage limits for each channel.
 *         Note that these values limit the requested voltage not the 
 *         actual voltage.  Any VSET > SVMAX is turned into an SVMAX setting.
 */
string
CV6533::getSoftwareVmax()
{
  size_t            nRead;
  error_code        status;
  
  uint32_t           sVmax[6];
  
  for (int i=0; i < 6; i++) {
    status = m_pVme->vmeRead(
      getBase() +  Channels[i] + SVMax, sVmax[i], amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read software VMAX register for channel " << i
        << " : " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  
  return scaledIToString(sVmax, 0.1);
}
/**
 * Get the ramp down rates.
 *
 * @return string
 * @retval Tcl list that contains the ramp down rates in V/seconds for each channel.
 */
string
CV6533::getRampDownRates()
{
  size_t            nRead;
  error_code        status;
  
  uint32_t          rdRates[6];

  for (int i=0; i < 6; i++) {
    status = m_pVme->vmeRead(
      getBase() + Channels[i] + RampDown, rdRates[i], amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read ramp down rate for channel:  " << i
        << " : " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
   
  return scaledIToString(rdRates,1.0);
}
/**
 * Get the ramp up rates.
 *
 * @return string
 * @retval Tcl list that contains the ramp up rates in V/seconds 
 *         for each channel.
 */
string
CV6533::getRampUpRates()
{
  size_t            nRead;
  error_code        status;
  
  uint32_t          rupRates[6];

  for (int i= 0; i < 6; i++) {
    status = m_pVme->vmeRead(
      getBase() + Channels[i] + RampUp, rupRates[i], amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg; 
      smsg << "Unable to read ramp up rate for channel : " << i
        << " : " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  
  return scaledIToString(rupRates, 1.0);
}
/**
 **  Get the power down modes
 **
 ** @return string
 ** @retval properly formatted list of rampdown modes for each channel.
 **         the value for each channel is  either "ranmp" or "kill".
 */
string
CV6533::getPowerdownModes()
{
  size_t            nRead;
  error_code        status;
  
  uint32_t          modes[6];
  CTCLInterpreter   interp;
  CTCLObject        objResult;
  string            result;

  objResult.Bind(interp);         // Needed for list handlnig.

  for (int i=0; i < 6; i++) {
    status= m_pVme->vmeRead(
      getBase() + Channels[i] + PwDown, modes[i], amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read the power down mode for channel  " << i
        << " : " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  
  for(int i =0; i < 6; i++) {
    objResult += string(modes[i] ? "ramp" : "kill");
  }
  result = static_cast<string>(objResult);

  return result;
}
/**
 * Get the temperatures for each channel.  This will also update the
 * m_tempuratures member so that if there are requests for monitored value
 * between the time we are called and the next monitor list execution,
 * the updated values will be used.
 *
 * @return string
 * @retval Properly formatted Tcl list of temperatures for each channel.
 */
string
CV6533::getTemperatures()
{
  size_t             nRead;
  error_code         status;
  
  
  for (int i =0; i < 6; i++) {
    status = m_pVme->vmeRead(
      getBase() + Channels[i] + Temp, m_temperatures[i], amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read the temperature of channel " << i
        << " :  " << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  
  return scaledIToString(m_temperatures, 1.0);
}

/**
 * Get the channel polarities.
 *
 * @return string
 * @retval Tcl list of strings "+" for positive "-" for negative.
 */
string
CV6533::getPolarities()
{
  size_t             nRead;
  error_code                status;
  
  uint32_t           polarityValues[6];
  CTCLInterpreter    interp;	// captive interpreter for formattting.
  CTCLObject         objResult;
  string             stringResult;

  for (int i=0; i < 16; i++) {
    status = m_pVme->vmeRead(
      getBase() + Channels[i] + Polarity, polarityValues[i], amod, VMEDataWidth::D16
    );
    if (status) {
      stringstream smsg;
      smsg << "Unable to read channel " << i << "'s polarity: "
        << status.message();
      string msg(smsg.str());
      throw msg;
    }
  }
  objResult.Bind(interp);
  for (int i =0; i < 6; i++) {
    objResult += polarityValues[6] ? string("+") : string("-");
  }
  stringResult = static_cast<string>(objResult);
  return stringResult;
}
/**
 * fillMonitoredVariables
 * Add our list to the monitoring functions.  Unfortunately, the whole
 * layout of the register set and 16 bitedness means we're going to be doing
 * single shot transfers rather than blocks.
 * @param vmeList - VME readout list to which we will be adding data.
 */
void
CV6533::fillMonitoredVariables()
{
  uint32_t base  = getBase();
  // Global status:

  error_code status;

  status = m_pVme->vmeRead(base + BoardStatus, m_globalStatus, amod, VMEDataWidth::D16);
  if (status) {
    stringstream smsg;
    smsg << "Failed to read module global status register : " << status.message();
    string msg(smsg.str());
    throw msg;
  }
  
  // Channel stuff will be grouped by channel so that looping can be done both here and
  // when unpacking:

  for(int i =0; i < 6; i++) {
    status  = m_pVme->vmeRead(
      base + Channels[i] + ChStatus, m_channelStatus[i], amod, VMEDataWidth::D16
    ); // Channel status.
    if (status) {
      stringstream smsg;
      smsg << " Could not read channel status for channel " << i
        << " :  " << status.message();
      string m(smsg.str());
      throw m;
    }

    status = m_pVme->vmeRead(
      base + Channels[i] + VMon,  m_voltages[i],   amod, VMEDataWidth::D16
    ); // Actual voltage.
    if (status) {
      stringstream smsg;
      smsg << "Could not read chanelk " << i << " actual voltage: " << status.message();
      string m(smsg.str());
      throw m;
    }

    status = m_pVme->vmeRead(
      base + Channels[i] + IMon, m_currents[i],   amod, VMEDataWidth::D16
    ); // Monitored current
    if (status) {
      stringstream smsg;
      smsg << "Could not read channel " << i << "'s current: " << status.message();
      string m(smsg.str());
      throw m;
    }

    status = m_pVme->vmeRead(
      base + Channels[i] + Temp, m_temperatures[i], amod, VMEDataWidth::D16
    ); // Channel temperature.
    if (status) {
      stringstream smsg;
      smsg << "Could not read the temperature of channel " << i << " : " << status.message();
      string m(smsg.str());
      throw m;
    }
  }

}