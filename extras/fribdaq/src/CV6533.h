/*
  @file CV6533.h
  @brief definitions Slow controls driver for the CAEN V6533 VME module.

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

#ifndef MVLC_CV6533_H
#define MVLC_CV6533_H

#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <string>
#include <stdint.h>

namespace mesytec {
  namespace mvlc {
    class MVLC;
  }
}

class CVMUSB;
class CVMUSBReadoutList;
/**
 *  Controls the CAEN V 6533 6 channel HV module.  Parameters are:
 *   - globalmaxv    - Board maximum voltage
 *   - globalmaxI    - Board maximum current.
 *   - vn            - requested voltage for channel n.
 *   - in            - requested current max for channel n.
 *   - onn           - On/Off for channel n
 *   - vactn         - Actual voltage for channel n.
 *   - iactn         - Actual current for channel n
 *   - statusn       - Read only - get status for channel n.
 *   - ttripn        - Trip  time for channel n.
 *   - svmaxn        - Voltage max for channel n.
 *   - rdownn        - Ramp down rate for channel n.
 *   - rupn          - Ramp up rate for channel n.
 *   - pdownmoden    - Set the power down mode for channel n (ramp or kill).
 *   - polarityn     - Polarity for channel n.
 *   - tempn         - Temperature of channel n.
 */

class CV6533 : public SlowControlsDriver
{
private:

  // The following registers are read via our periodic monitor list:

  uint32_t          m_globalStatus;
  uint32_t          m_channelStatus[6];
  uint32_t          m_voltages[6];
  uint32_t          m_currents[6];
  uint32_t          m_temperatures[6];

  // Canonical methods:
public:
  CV6533(mesytec::mvlc::MVLC* controller);
  virtual ~CV6533();
private:
  CV6533(const CV6533& rhs);
  CV6533& operator=(const CV6533& rhs);
  int operator==(const CV6533& rhs) const;
  int operator!=(const CV6533& rhs) const;

  // Overrides:
public:
  
  virtual void reconfigure();	                     //!< Optional initialization.
  virtual void Update() ;               //!< Update module.
  virtual std::string Set(
			  const char* parameter, 
			  const char* value) ;            //!< Set parameter value
  virtual std::string Get(
			  const char* parameter) ;        //!< Get parameter value.
  virtual std::string getMonitor();

  // Local utilities: 

private:
  
  uint32_t getBase();		// Get the base address of the module from config

  // Functions used to set the module parameters.

  void turnOff( unsigned int channel);
  void setRequestVoltage( unsigned int channel, float value);
  void setRequestCurrent( unsigned int channel, float value);
  void setChannelOnOff( unsigned int channel, bool value);
  void setTripTime( unsigned int channel, float time);
  void setMaxVoltage( unsigned int channel, float value);
  void setRampDownRate( unsigned int channel, float rate);
  void setRampUpRate( unsigned int channel, float rate);
  void setPowerDownMode( unsigned channel, std::string mode);

  // functions used to read module parameters.

  std::string getGlobalMaxV();
  std::string getGlobalMaxI();
  std::string getChannelVoltages();
  std::string getChannelCurrents();
  std::string getOnOffRequests();
  std::string getActualVoltages();
  std::string getActualCurrents();
  std::string getChannelStatuses();
  std::string getTripTimes();
  std::string getSoftwareVmax();
  std::string getRampDownRates();
  std::string getRampUpRates();
  std::string getPowerdownModes();
  std::string getTemperatures();
  std::string getPolarities();

  bool strToBool(std::string value);
  std::string fToString(float value);
  std::string scaledIToString(uint32_t* values, float scaleFactor);
  void fillMonitoredVariables();

};


#endif
