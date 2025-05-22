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

#include <config.h>
#include "CGDG.h"
#include "CControlModule.h"
#include "CVMUSB.h"
#include "CVMUSBReadoutList.h"	// for the AM codes.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


using namespace std;

// MDD8 register etc. definitions.

#define CONST(name) static const uint32_t name = 

static const uint8_t  am (CVMUSBReadoutList::a24UserData); // Address modifier.
CONST(Firmware)     0x00;
CONST(GlobalReg)    0x04;
CONST(AuxReg)       0x08;
CONST(ActionReg)    0x80;
CONST(FGGConfig)    0x84;
CONST(SclrConfig)   0x88;
CONST(FGGTrigSel1)  0x8c;
CONST(FGGTrigSel2)  0x90;
CONST(NIMOSel1)     0x94;
CONST(NIMOSel2)     0x98;
CONST(GRST1)        0x9c;
CONST(GRST2)        0xa0;
CONST(SclInSel1)    0xa4;
CONST(SclInSel2)    0xa8;
CONST(ComboGateMask1) 0xac;
CONST(ComboGateMask2) 0xb0;
CONST(Scalers)      0x100;   // through 0x13f inclusive.
CONST(CoincData)    0x140; 

// Note the gate and delay registers are paired I give the
// first one of each but you then have to
// add 8*n not 4*n to get to subsequent registers:

CONST(Delay0)      0x40;
CONST(Gate0)       0x44;

// FGG register field codes

CONST(CONFIG_OFF)    0;
CONST(CONFIG_DGG)    1;
CONST(CONFIG_SRG)    2;
CONST(CONFIG_PG)     3;
CONST(CONFIG_RDGG)   4;
CONST(CONFIG_PSG)    5;
CONST(CONFIG_CPSG)   6;

// And shifts to position:

CONST(CONFIG_1)      0;
CONST(CONFIG_2)      4;
CONST(CONFIG_3)      8;
CONST(CONFIG_4)      12;
CONST(CONFIG_5)      16;
CONST(CONFIG_6)      20;
CONST(CONFIG_7)      24;
CONST(CONFIG_8)      28;

// Trigger select/output select codes and field shifts:

CONST(SEL_NIM1)      0;      // Inputs
CONST(SEL_NIM2)      1;
CONST(SEL_NIM3)      2;
CONST(SEL_NIM4)      3;
CONST(SEL_NIM5)      4;
CONST(SEL_NIM6)      5;
CONST(SEL_NIM7)      6;
CONST(SEL_NIM8)      7;

CONST(SEL_Gate1)      0;    // Outputs
CONST(SEL_Gate2)      1;
CONST(SEL_Gate3)      2;
CONST(SEL_Gate4)      3;
CONST(SEL_Gate5)      4;
CONST(SEL_Gate6)      5;
CONST(SEL_Gate7)      6;
CONST(SEL_Gate8)      7;

CONST(SEL_Edge1)     8;   // Inputs or outputs.
CONST(SEL_Edge2)     9;
CONST(SEL_Edge3)    10;
CONST(SEL_Edge4)    11;
CONST(SEL_Edge5)    12;
CONST(SEL_Edge6)    13;
CONST(SEL_Edge7)    14;
CONST(SEL_Edge8)    15;

CONST(SEL_CG1)      16;  // Inputs or outputs.
CONST(SEL_CG2)      17;
CONST(SEL_CG3)      18;
CONST(SEL_CG4)      19;

CONST(SEL_Ctl)      20;  // inputs only.

// Shift counts to fields:

CONST(SEL_1)         0;
CONST(SEL_2)         8;
CONST(SEL_3)        16;
CONST(SEL_4)        24;

/*!
  Construction is pretty much a no-op as the configuration is 
  handled at attach time.
*/
CGDG::CGDG() :
  CControlHardware(),
  m_pConfiguration(0)
{
  for (int i=0; i < 8; i++) {
    m_delays[i] = m_widths[i] = 0; // Testing.
  }
}
/*!
  Same for copy construction.. however this is done by clone just for
  regularity.
*/
CGDG::CGDG(const CGDG& rhs)  : 
  CControlHardware(rhs)
{
}
/*!
   Destruction is also a no-op.
*/
CGDG::~CGDG()
{
}
/*!
   Assignment is a clone:
*/
CGDG&
CGDG::operator=(const CGDG& rhs) 
{
  if (this != &rhs) {
    CControlHardware::operator=(rhs);
  }
  return *this;
}
/*!
   All GDG's with the same configuration are equivalent.
*/
int
CGDG::operator==(const CGDG& rhs) const
{
  return CControlHardware::operator==(rhs);
}
int
CGDG::operator!=(const CGDG& rhs) const
{
  return CControlHardware::operator!=(rhs);
}
//////////////////////////////////////////////////////////////////////////

/*!
    onAttach is called when the module is attached to a configuration.
    The configuration is set up with the following
    configuration values:
    - base  - Module base address.
*/
void
CGDG::onAttach(CControlModule& configuration)
{
  m_pConfiguration = &configuration;
  configuration.addParameter("-base", XXUSB::CConfigurableObject::isInteger, NULL, 
			     string("0"));

}
/*!
  Initialization just calls update:
*/
void
CGDG::Initialize(CVMUSB& vme)
{
  Update(vme);
}
/*!
   Update - This updates any internal state about the module that we would
   keep.  It may be, for example that for speed sake we'll cache all the
   module parameters and return them on a get... until one has been changed.
   Update ensures that the internal state is coherent with the module
   hardware state.
   \param vme : CVMUSB&
      Vme controller used to talk with the module.
   \return std::string
   \retval "OK" on success.
*/
string
CGDG::Update(CVMUSB& vme)
{
  uint32_t baseAddress = base();
  
  // We are going to ensure that the module is set to be
  // a gate and delay generator by setting up the trigger select and
  // output select registers, as well as the configuration 
  // register.  We'll then read the delays and width values
  // in a block read.
  //
  // So that this doesn't take all day we'll do this as an immediate
  // list:

  unique_ptr<CVMUSBReadoutList> pOps(vme.createReadoutList());
  CVMUSBReadoutList& ops = *pOps;
  ops.addWrite32(baseAddress+FGGConfig,
		 am, 
		 (CONFIG_DGG << CONFIG_1)    |
		 (CONFIG_DGG << CONFIG_2)    |
		 (CONFIG_DGG << CONFIG_3)    |
		 (CONFIG_DGG << CONFIG_4)    |
		 (CONFIG_DGG << CONFIG_5)    |
		 (CONFIG_DGG << CONFIG_6)    |
		 (CONFIG_DGG << CONFIG_7)    |
		 (CONFIG_DGG << CONFIG_8)); //  All channels are simple GDGs.
  ops.addWrite32(baseAddress+FGGTrigSel1,      //  Set consecutive nim inputs
		 am,                           // as consecutive gate channel
		 (SEL_NIM1 << SEL_1)         | // triggers...
		 (SEL_NIM2 << SEL_2)         |
		 (SEL_NIM3 << SEL_3)         |
		 (SEL_NIM4 << SEL_4));
  ops.addWrite32(baseAddress+FGGTrigSel2, 
		am,
		(SEL_NIM5   << SEL_1)        |
		(SEL_NIM6   << SEL_2)        |
		(SEL_NIM7   << SEL_3)        |
		(SEL_NIM8   << SEL_4));        // ...<
  ops.addWrite32(baseAddress+NIMOSel1,
		 am,
 		 (SEL_Gate1 << SEL_1)         | // Output selections...
		 (SEL_Gate2 << SEL_2)         |
		 (SEL_Gate3 << SEL_3)         |
		 (SEL_Gate4 << SEL_4)); 
  ops.addWrite32(baseAddress+NIMOSel2,
		 am,
 		 (SEL_Gate5 << SEL_1)         | 
		 (SEL_Gate6 << SEL_2)         |
		 (SEL_Gate7 << SEL_3)         |
		 (SEL_Gate8 << SEL_4)); 

  // Read the delays and gates.

  uint32_t delayOffset = Delay0;
  uint32_t gateOffset  = Gate0;

  for (int i=0; i < 8; i++) {
    ops.addRead32(base() + delayOffset, am);
    ops.addRead32(base() + gateOffset, am);
    delayOffset += 8;
    gateOffset  += 8;
  }
  // Do the operation and distribute the data to the members:

  uint32_t inputData[16];	// Data buffer for the list.
  size_t   readSize;
  int status = vme.executeList(ops, inputData, sizeof(inputData), &readSize);
  if (status < 0) {
    return string("ERROR - Could not execute Update List");
  }
  
  for (int i=0; i < 8; i++) {
    m_delays[i] = inputData[i*2];   // delays are the evens.
    m_widths[i] = inputData[1+i*2]; // widths are the odds.
  }

  return string("OK");
}
/*!
  set a parameter value. All values must be integers, and the parameters
  must be of one of the following forms:
  - delay[0-nchan-1]
  - width[0-nchan-1].

  \param vme  : CVMUSB&
     VME controller that connects to the VME crate this module is in.
  \param parameter : std::string
     Name of the parameter to modify.
  \param value     : std::string
     integer value to set the parameter to.

  \return std::string
  \retval ERROR - some text   - a problem was detected.
  \retval value - Success.

*/
string
CGDG::Set(CVMUSB& vme, string parameter, string value)
{

  unsigned int channelNumber;
  unsigned int parameterValue;

  // First decode the value as a uint32_t:

  if(sscanf(value.c_str(), "%i", &parameterValue) <= 0) {
    return string("ERROR - Value is not an integer and must be");
  }                       // May need to add range checking.

  // See if the parameter is a width:n:

  if (sscanf(parameter.c_str(), " width%u", &channelNumber) == 1) {
    return setWidth(vme, channelNumber, parameterValue);
  }
  else if(sscanf(parameter.c_str(), " delay%u", &channelNumber) == 1) {
    return setDelay(vme, channelNumber, parameterValue);
  }
  else {
    return string("ERROR - parameter specifier invalid");
  }
  
}
/*!
   Get a parameter value and return it to the caller
   \param vme : CVMUSB&
       reference to the VME controller that talks to the device.
   \param parameter : std::string
       Name of the paramter to retrieve.  Must be of the form
       - delay%u  To get a delay value. 
       - width%u  To get a width value.
   \return std::string
   \retval ERROR - yadayadya    Reports an error.
   \retval some unsigned integer Reports the value read.
*/
string
CGDG::Get(CVMUSB& vme, string parameter)
{
  unsigned int channelNumber;
  
  if(sscanf(parameter.c_str(), " width%u", &channelNumber) == 1) {
    return getWidth(vme, channelNumber);
  }
  else if (sscanf(parameter.c_str(), " delay%u", &channelNumber) == 1) {
    return getDelay(vme, channelNumber);
  }
  else {
    return string("ERROR - parameter specifier invalid");
  }
}
     
/*!
    Clone oursevles... a no op at this point
*/
CControlHardware*
CGDG::clone() const
{
  return (new CGDG(*this));
}

////////////////////////////////////////////////////////////////////////////

/*
   Retrieve the value of the base parameter.
   Note that if there is no configuration, we return 0 (uninitialized value).

*/
uint32_t 
CGDG::base()
{
  if (m_pConfiguration) {
    string strBase = m_pConfiguration->cget("-base");
    unsigned long base;
    base = strtoul(strBase.c_str(), NULL, 0);
    return static_cast<uint32_t>(base);
  } 
  else {
    return static_cast<uint32_t>(0);
  }
}
/*
    Set the delay of one of the channels.
    Parameters:
       CVMUSB& vme          - The vme controller communicating with the device.
       unsigned int channel - The channel number (0-7).
       unsigned int value   - The new value
    Returns:
       Stringified value set or an error.
*/
string
CGDG::setDelay(CVMUSB& vme, unsigned int channel, unsigned int value)
{
  if (channel > 7) {
    return string ("ERROR - invalid channel");
  }
  else {
    m_delays[channel] = value;
    int status = vme.vmeWrite32(base() + Delay0 + channel*8, am, value);
    if (status != 0) {
      return string("ERROR - Failed on Vme write32");
    } else {
      return string("OK");
    }
  }
}
/*
   Set the width of one of the channels.
    Parameters:
       CVMUSB& vme          - The vme controller communicating with the device.
       unsigned int channel - The channel number (0-7).
       unsigned int value   - The new value
    Returns:
       Stringified value set or an error.
*/
string
CGDG::setWidth(CVMUSB& vme, unsigned int channel, unsigned int value)
{
  if (channel > 7) {
    return string("ERROR - invalid channel");
  }
  else {
    m_widths[channel] = value;
    int status = vme.vmeWrite32(base() + Gate0 + channel*8, am, value);
    if (status != 0) {
      return string("ERROR - Failed on VME write32");
    } else {
      return string("OK");
    }
  }

}

/*
   Get the value of a channel delay.
   Parameters:
    CVMUSB& vme           - Vme controller communicating with the device.
    unsigned int channel  - Channel number (0-7)

   Returns:
    stringified value or an error message.
*/
string
CGDG::getDelay(CVMUSB& vme, unsigned int channel)
{
  if (channel > 7) {
    return string("ERROR - invalid channel");
  }
  else {
    int status = vme.vmeRead32(base() + Delay0 + channel*8, am,
			       &(m_delays[channel]));
    if (status != 0) {
      return string("ERROR - vme read 32 failed");
    }
    char msg[100];
    sprintf(msg, "%d", m_delays[channel]);
    return string(msg);
  }
}
/*
  Get the value of a channel width.
   Parameters:
    CVMUSB& vme           - Vme controller communicating with the device.
    unsigned int channel  - Channel number (0-7)
   Returns:
    stringified value or an error message.
*/
string
CGDG::getWidth(CVMUSB& vme, unsigned int channel)
{
  if (channel > 7) {
    return string("ERROR - invalid channel");
  }
  else {
    int status = vme.vmeRead32(base() + Gate0 + channel*8, am,
			       &(m_widths[channel]));
    if (status != 0) {
      return string("ERROR - vme read 32 failed");
    }
    char msg[100];
    sprintf(msg, "%d", m_widths[channel]);
    return string(msg);
  }
}
