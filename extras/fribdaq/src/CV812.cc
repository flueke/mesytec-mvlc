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


#include "CV812.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <XXUSBConfigurableObject.h>

#include <stdint.h>
#include <stdio.h>
#include <tcl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sstream>
#include <iostream>

using namespace std;
using mesytec::mvlc::VMEDataWidth;
using mesytec::mvlc::MVLC;
using XXUSB::CConfigurableObject;
// Register offsets:

#ifdef Const
#undef Const
#endif

#define Const(name) static const uint32_t name = 

static const uint8_t  am (0x09); // Address modifier: non priv A32 data.



Const(Thresholds)   0x0;	// There are 16 16-bit words of these.
Const(Widths)       0x40;	// There are 2 16-bit words of these.
Const(DeadTimes)    0x44;	// There are 2 16 bit words of these.
Const(Majority)     0x48;	// There's only one of these.
Const(Inhibits)     0x4a;	// Only one of these.
Const(TestPulse)    0x4c;
Const(FixedCode)    0xfa;
Const(MfgAndModel)  0xfc;
Const(VersionAndSerial) 0xfe;

// Fields in the discription block.

Const(VersionMask)  0xf000;
Const(SerialMask)   0x0fff;
Const(VersionShift) 12;

Const(MfgMask)      0xfc00;
Const(MfgValue)     0x0800;	// unshifted
Const(TypeMask)     0x03ff;
Const(V812Type)     0x0051;
Const(V895Type)     0x0054;

Const(FixedCodeValue) 0xfaf5;

/*!
   construct the beast.. The shadow registers will all get set to zero
   TODO:  Might be that a non zero 'default' base address like 0xffffffff is better than
   0; since it's more clearly impossible.
*/
CV812::CV812(MVLC* pVme) :
 SlowControlsDriver(pVme),
  m_is812(true)
{
  for (int i =0; i < 16; i++) {
    m_thresholds[i] = 0;
  }
  for (int i=0; i < 2; i++) {
    m_widths[i]    = 0;
    m_deadtimes[i] = 0;
  }
  m_inhibits = 0xffff;		// 0 means inhibited.
  m_majority = 1;
}

/*!
  Config is managed by the base class.
*/
CV812::~CV812()
{
}

///////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////
/*!
    reconfigure the module bringing it to a known, generally called after the
    -base or -file have been set.
    If a configuration file exists we read it in and process it to
    load our shadow configuration.  Regardless, the shadow configuration gets
    loaded into the device so that the discriminator is in a known state.

   \param CVMUSB& vme
     Controller that hooks us to the VM-USB.
*/
void
CV812::reconfigure()
{
  auto& vme(*m_pVme);                    // Reduce mistyping.
  // Figure out the model type.

  uint32_t serialVersion;
  uint32_t mfgType;
  uint32_t fixedCode;

  // Assume that nobody uses -base 0:

  if (!base()) return;                    // Base not configured yet.

  auto stat1 = vme.vmeRead(base() + VersionAndSerial, serialVersion, am, VMEDataWidth::D16);
  auto stat2 = vme.vmeRead(base() + MfgAndModel, mfgType, am, VMEDataWidth::D16);
  auto stat3 = vme.vmeRead(base() + FixedCode,  fixedCode, am,  VMEDataWidth::D16);

  if(bool(stat1 )| bool(stat2) | bool(stat3)) {
    cerr << "V812/V895 failed to read the module descriptor block for base: "
	 << hex << base() << dec << endl;
  }
  else {
    // Describe the module:

    if (fixedCode != FixedCodeValue) {
      cerr << "V812/V895 fixed code value was : " << hex << fixedCode 
	   << " should have been: " << FixedCodeValue 
	   << " base address " << base() << dec << endl;
    }
    else {
      uint16_t serial  = serialVersion & SerialMask;
      uint16_t version = (serialVersion & VersionMask) >> VersionShift;
      uint16_t mfg     = mfgType & MfgMask;
      uint16_t type    = mfgType & TypeMask;

      if ((mfg != MfgValue) || 
	      ((type != V812Type) && (type != V895Type))) {
        cerr << "V812/V895 has bad Manufacturer or type code\n";
        cerr << "Manufacturer should be " << MfgValue << " was " << mfg << endl;
        cerr << "Type was " << hex << type 
            << "should be one of " << V812Type << " or " << V895Type 
            << " base address: " << base() << dec << endl;
            }
            else {
        m_is812 = (type == V812Type) ? true : false;

        cerr << "Located valid CAEN V" 
            << (m_is812 ? "812" : "895") << " module, Serial number: " << serial 
            << " version " << version << " at base address " 
            << hex << base() << dec << endl;
      }
    }
  }
  // Initialize the configuration:

  configFileToShadow();		// Process the config file if necessary.
  Update();			// Load the shadow registers into the device.
}

/*!
  Update the device from the shadow configuration.

  
  \return string
  \retval "OK"  - Everything worked.
  \retval "ERROR -...."  Describes why things did not work.

*/
void
CV812::Update()
{
  auto& vme(*m_pVme);                      // Less changes I hope.
  uint32_t baseAddress = base();
  if (baseAddress == 0) {
    return;                                // assume it's not yet configured.
  }

  for (int i=0; i < 16; i++) {
    vme.vmeWrite(baseAddress + Thresholds + (i*sizeof(uint16_t)),
		   (uint16_t)-m_thresholds[i],
      am, VMEDataWidth::D16);
  }
  for (int i =0; i < 2; i++) {
    vme.vmeWrite(baseAddress + Widths + (i*sizeof(uint16_t)),
		   m_widths[i], am, VMEDataWidth::D16);
    if (m_is812) {
      vme.vmeWrite(baseAddress + DeadTimes + (i*sizeof(uint16_t)),
		    m_deadtimes[i], am, VMEDataWidth::D16);
    }
  }
  vme.vmeWrite(baseAddress + Inhibits,  m_inhibits, am, VMEDataWidth::D16);
  vme.vmeWrite(baseAddress+ Majority, majorityToRegister(m_majority), 
    am, VMEDataWidth::D16
	);

}
///////////////////////////////////////////////////////////////////////////////////
/*!
   Set a value in the device.

  The parameter must be an integer.  For the inhibits it must be between
  0-0xffff, for the majority threshold it must be between 1 and 20.  For
  the other parameters in the range 0-0xff.

  \param parameter  - Name of the parameter to set.
  \param value      - Value to which the parameter will be set.

\note  There are also restrictions on the parameter name and the channel number of
      the parameter name.

   \return string
   \retval "OK"    - The parameter was set (the shadow value will also be set).
   \retval "ERROR -..."  An error was detected, the remaining string describes the error.
*/
string
CV812::Set(const char* parameter, const char*  value)
{

  unsigned int channelNumber;
  unsigned int parameterValue;

  // First decode the parameter value.. if not an integer we can stop right now:

  if (sscanf(value, "%i", &parameterValue) <= 0) {
    stringstream smsg;
    smsg << "Error - The value " << parameter << " is not an integer but must be";
    string msg(smsg.str());
    return msg;
  }
  // Now figure out whast sort of parameter this is and dispatch...or an error
  // if no parameter matches:

  if (sscanf(parameter, "threshold%u", &channelNumber) == 1) {
    return setThreshold(channelNumber, parameterValue);
  }
  else if (sscanf(parameter, "width%u", &channelNumber) == 1) {
    return setWidth(channelNumber, parameterValue);
  }
  else if (sscanf(parameter, "deadtime%u", &channelNumber) == 1) {
    return setDeadtime(channelNumber, parameterValue);
  }
  else if (parameter == string("inhibits")) {
    return setInhibits( parameterValue);
  }
  else if (parameter == string("majority")) {
    return setMajority( parameterValue);
  } 
  else {
    stringstream smsg;
    smsg << "ERROR - "  << parameter << " is not a known parameter name";
    string msg(smsg.str());
    return msg;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////
/*!
   Get a value from the device.
   Since the actual device is read-only we will return a value from the shadow state
   of the device.  This should be accurate as long as the crate has not been power-cycled...
   and since a power cycle would invalidate the VM_USB controller object that's a good enough 
  condition.

   \param parameter - The name of the desired parameter.
 
  \return string
  \retval stringified-integer   - Things went ok.
  \retval "ERROR - ..."         - An error occured described by the remainder of the string.
*/
string
CV812::Get(const char* parameter)
{
  unsigned int channelNumber;

  if (sscanf(parameter, "threshold%u", &channelNumber) == 1) {
    return getThreshold(channelNumber);
  }
  else if (sscanf(parameter, "width%u", &channelNumber) == 1) {
    return getWidth(channelNumber);
  }
  else if (sscanf(parameter, "deadtime%u", &channelNumber) == 1) {
    return getDeadtime(channelNumber);
  }
  else if (parameter == string("inhibits")) {
    return getInhibits();
  }
  else if (parameter == "majority") {
    return getMajority();
  }
  else {
    stringstream smsg;
    smsg << "ERROR - " << parameter << " is not a valid parameter name";
    string msg(smsg.str());
    return msg;
  }

}


//////////////////////////////////////////////////////////////////////////////////
///////////////// private utilities //////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

/*
   Return the base address of the device:

*/
uint32_t 
CV812::base()
{
  
  return getConfiguration()->getIntegerParameter("-base");
}
/*
  Return the name of the file to process for the initial values of the 
  shadow register set:
*/
string
CV812::initializationFile()
{
  return getConfiguration()->cget("-file");
  
}
/*
  Process the configuration file to the shadow registers.
  Errors are silent, including:
  - The file is not readable.
  - The file does not parse with the Tcl parser.
  - The file does not contain some or all of the values [we'll copy those that have been defined].

  The configuration file is a tcl script that should set the following arrays/variables:

   thresholds(i)  - The values of the thresholds.
   widths(i)      - The values of the two widths.
   deadtimes(i)   - The values of the two deadtimes.
   enables        - The enables mask.
   majority       - The majority threshold.

*/
void
CV812::configFileToShadow()
{
  string filename = initializationFile();

  Tcl_Obj* pObj = Tcl_NewStringObj(filename.c_str(), strlen(filename.c_str()));

  Tcl_IncrRefCount(pObj);	// Evidently FSAccess needs this.

  if (Tcl_FSAccess(pObj, R_OK)) {
    Tcl_DecrRefCount(pObj);
    return;			// Not allowed to read the file..or does not exist.
  }
  Tcl_DecrRefCount(pObj);

  // Simplest actually to do this in a non  object manner:

  Tcl_Interp* pInterp = Tcl_CreateInterp();
  int status          = Tcl_EvalFile(pInterp, filename.c_str());

  if (status != TCL_OK) {
    std::string msg = Tcl_GetStringResult(pInterp);
    std::cerr << "Error processing CAENV812 config file: " << filename << " :\n";
    std::cerr << msg << std::endl;
    std::cerr << "The discriminator might not be setup the way  you think it is\n";
  }

  // The threshold array:

  for (int i=0; i < 16; i++) {
    const char* value = Tcl_GetVar2(pInterp, "thresholds", iToS(i).c_str(), TCL_GLOBAL_ONLY);
    if (value) {
      int iValue;
      if (sscanf(value, "%i", &iValue) == 1) {
	      m_thresholds[i] = iValue;
      }
    }
  }
  // The widths array:

  for (int i=0; i < 2; i++) {
    const char* value = Tcl_GetVar2(pInterp, "widths", iToS(i).c_str(), TCL_GLOBAL_ONLY);
    if (value) {
      int iValue;
      if (sscanf(value, "%i", &iValue) == 1) {
	      m_widths[i] = iValue;
      }
    }
  }
  // Deadtimes:


  if (m_is812) {

    for (int i=0; i < 2; i++) {
      const char* value = Tcl_GetVar2(pInterp, "deadtimes", iToS(i).c_str(), TCL_GLOBAL_ONLY);
      if (value) {
        int iValue;
        if (sscanf(value, "%i", &iValue) == 1) {
          m_deadtimes[i] = iValue;
        }
      }
    }
  }

  // enables and majority

  const char* value = Tcl_GetVar(pInterp, "enables", TCL_GLOBAL_ONLY);
  if (value) {
    int iValue;
    if (sscanf(value, "%i", &iValue) == 1) {
      m_inhibits = iValue;
    }
  }
  value = Tcl_GetVar(pInterp, "majority", TCL_GLOBAL_ONLY);
  if (value) {
    int iValue;
    if (sscanf(value, "%i", &iValue) == 1) {
      m_majority = iValue;
    }
  }
  Tcl_DeleteInterp(pInterp);

}
/*
  Set a threshold value:
  Parameters:
    
    channel        - Which one to set (error if not in [0, 15]).
    value          - Value to set (error if not in [0,0xff]).

  Returns:
    string("OK")   - Everything worked.
    string("ERROR - ...") There was a problem.  The string describes the problem.
*/

string
CV812::setThreshold(unsigned int channel, uint16_t value) 
{
  auto& vme(*m_pVme);
  // Validate parameters:

  if (channel >= 16) {
    return string("ERROR - Invalid channel number [0-15]");
  }
  if (value > 0xff) {
    return string("ERROR - Invalid threshold value [0-255]");
  }

  // set the hardware:

  auto status = vme.vmeWrite(base() + Thresholds + channel*sizeof(uint16_t),
			  value, am, VMEDataWidth::D16);
  if (status) {
    stringstream smsg;
    smsg << "ERROR - Failed to write a CAEN Discriminator device sestting: " << status.message();
    string msg(smsg.str());
    return msg;
  
  }

  // set the shadow register:

  m_thresholds[channel ] = value;

  return string("OK");
}
/*
  Set a width value.
  Parameters:
   
   selector - Which width to set 0 - 0-7, 1, 8-15.
   value    - New width value. [0,0xff]

  Returns:
    string("OK")   - Everything worked.
    string("ERROR - ...") There was a problem.  The string describes the problem
*/
string
CV812::setWidth(unsigned int selector, uint16_t value)
{
  auto& vme(*m_pVme);

  // validation:

  if (selector > 1) {
    return string("ERROR - Invalid width selector");
  }
  if (value > 0xff) {
    return string("ERROR - Invalid width value [0..255]");
  }

  // set the hardware

  auto  status = vme.vmeWrite(base() + Widths + selector*sizeof(uint16_t),
			  value, am, VMEDataWidth::D16);
  if (status) {
    stringstream smsg;
    smsg << "ERROR - Write to CAEN discriminator device failed : " << status.message();
    string msg(smsg.str());
    return msg;
    
  }

  // set the shadow register.

  m_widths[selector] = value;

  return string("OK");
}

/*!
  Set a deadtime value
  Parameters:
   
   selector - Which deadtime to set 0 - 0-7, 1, 8-15.
   value    - New deadtime value. [0,0xff]

  Returns:
    string("OK")   - Everything worked.
    string("ERROR - ...") There was a problem.  The string describes the problem
*/
string
CV812::setDeadtime(unsigned int selector, uint16_t value)
{
  auto& vme(*m_pVme);
  // validation:

  if (!m_is812) {
    return string("ERROR - CAEN V895 modules have no deadtime");
  }

  if (selector > 1) {
    return string("ERROR - Invalid deadtime selector");
  }
  if (value > 0xff) {
    return string("ERROR - Invalid deadtime value [0..255]");
  }

  // set the hardware

  auto status = vme.vmeWrite(base() + DeadTimes + selector*sizeof(uint16_t),
			  value, am, VMEDataWidth::D16);
  if (status) {
    stringstream smsg;
    smsg << "ERROR - Write to CAEN discriminator devie failed: " << status.message();
    string msg(smsg.str());
    return msg;
    
  }

  // set the shadow register.

  m_deadtimes[selector] = value;

  return string("OK");
}
/*
  Set the majority logic value.  The majority is storead as a majority level that has to
  be converted to a register value.

  Parameters:
  
  value    - New majority logic value [1..20].


 Returns:
    string("OK")   - Everything worked.
    string("ERROR - ...") There was a problem.  The string describes the problem
*/
string
CV812::setMajority(uint16_t value)
{
  auto& vme(*m_pVme);
  // Validate:

  if ((value < 1)  || (value > 20)) {
    return string("ERROR - invalid majority level [1..20]");
  }
  // Set the hardware

  auto status  = vme.vmeWrite(
    base() + Majority, majorityToRegister(value), am, VMEDataWidth::D16
  );
  if(status) {
    stringstream smsg;
    smsg << "ERROR - Write to CAEN discdriminator device failed : " << status.message();
    string msg(smsg.str());
    return msg;
    
  }

  // set the shadow register.

  m_majority = value;

  return string("OK");
}
/*!
   Set the inhibits value 

  Parameters:
  
  value    - New inhibits value [0-255].


 Returns:
    string("OK")   - Everything worked.
    string("ERROR - ...") There was a problem.  The string describes the problem
*/
string
CV812::setInhibits(uint16_t value)
{

  auto& vme(*m_pVme);
  // Set the hardware

  auto status = vme.vmeWrite(base() + Inhibits, value, am, VMEDataWidth::D16);
  if (status) {
    stringstream smsg;
    smsg << "ERROR - Write to CAEN discdriminator device failed : " << status.message();
    string msg(smsg.str());
    return msg;
    
  }

  // set the shadow registers.

  m_inhibits = value;

  return string("OK");
}

/////////////////////////////////////////////////////////////////////////////////

/*
  return a threshold value.  

*/
string
CV812::getThreshold(unsigned int channel)
{
  // validate:

  if (channel >= 16) {
    return string("ERROR - invalid channel number");
  }

  return iToS(m_thresholds[channel]);
}
/*
  Return a width
*/
string
CV812::getWidth(unsigned int selector)
{
  if (selector > 1) {
    return string("ERROR - invalid width selector");
  }
  return iToS(m_widths[selector]);
}

/*
  Return a deadtime
*/
string
CV812::getDeadtime(unsigned int selector)
{
  if (!m_is812) {
    return string("ERROR - CAEN V895 modules have no deadtime");
  }
  if (selector > 1) {
    return string("ERROR - invalid deadtime selector");
  }
  return iToS(m_deadtimes[selector]);
}

/*!
  Return majority logic level
*/
string
CV812::getMajority()
{
  return iToS(m_majority);
}

/*
   Return inhibits (really enables)
*/
string
CV812::getInhibits()
{
  return iToS(m_inhibits);
}

/*
  Turn an integer into a string
*/

string
CV812::iToS(int value)
{
  char sValue[100];;
  sprintf(sValue, "%d", value);
  return string(sValue);
}



/*
  Turn the majority level into a desired register value
*/
uint16_t
CV812::majorityToRegister(int value)
{
  uint16_t result = (uint16_t)(((float)value*50.0 -25.0)/4.0 + 0.5);

  return result;
}


/////////////////////// The creator and registration:

SlowControlsDriver* 
V812Creator::create(MVLC* controller) {
  SlowControlsDriver* driver = new CV812(controller);
  auto config = driver->getConfiguration();
  config->addIntegerParameter("-base");
  config->addParameter("-file", nullptr, nullptr);

  return driver;
}

V812Creator::Register::Register() {
  SlowControlsFactory::getInstance()->addCreator("v812", new V812Creator);
}

static V812Creator::Register autoreg;