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


#include "CGDG.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <XXUSBConfigurableObject.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>


using namespace std;

using mesytec::mvlc::VMEDataWidth;
using mesytec::mvlc::MVLC;

// MDD8 register etc. definitions.
#ifdef CONST
#undef CONST
#endif
#define CONST(name) static const uint32_t name = 

static const uint8_t am (0x39); // Address modifier: A24 unpriv data.
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
CGDG::CGDG(MVLC* mvlc) :
  SlowControlsDriver(mvlc)
{
  for (int i=0; i < 8; i++) {
    m_delays[i] = m_widths[i] = 0; // Testing.
  }
}

/*!
   Destruction is  a no-op.
*/
CGDG::~CGDG()
{
}


//////////////////////////////////////////////////////////////////////////

/*!
   Update - This updates any internal state about the module that we would
   keep.  It may be, for example that for speed sake we'll cache all the
   module parameters and return them on a get... until one has been changed.
   Update ensures that the internal state is coherent with the module
   hardware state.
   
   \return std::string
   
*/
void
CGDG::Update()
{
  auto& vme(*m_pVme);                // Hoping to decrease the changes.
  uint32_t baseAddress = base();
  
  // We are going to ensure that the module is set to be
  // a gate and delay generator by setting up the trigger select and
  // output select registers, as well as the configuration 
  // register.  We'll then read the delays and width values
  // in a block read.
  //
  // So that this doesn't take all day we'll do this as an immediate
  // list:

  
  vme.vmeWrite(baseAddress+FGGConfig,
		 (CONFIG_DGG << CONFIG_1)    |
		 (CONFIG_DGG << CONFIG_2)    |
		 (CONFIG_DGG << CONFIG_3)    |
		 (CONFIG_DGG << CONFIG_4)    |
		 (CONFIG_DGG << CONFIG_5)    |
		 (CONFIG_DGG << CONFIG_6)    |
		 (CONFIG_DGG << CONFIG_7)    |
		 (CONFIG_DGG << CONFIG_8), am, VMEDataWidth::D32); //  All channels are simple GDGs.
  vme.vmeWrite(baseAddress+FGGTrigSel1,      //  Set consecutive nim input                          // as consecutive gate channel
		 (SEL_NIM1 << SEL_1)         | // triggers...
		 (SEL_NIM2 << SEL_2)         |
		 (SEL_NIM3 << SEL_3)         |
		 (SEL_NIM4 << SEL_4), am, VMEDataWidth::D32);
  vme.vmeWrite(baseAddress+FGGTrigSel2, 
		(SEL_NIM5   << SEL_1)        |
		(SEL_NIM6   << SEL_2)        |
		(SEL_NIM7   << SEL_3)        |
		(SEL_NIM8   << SEL_4), am, VMEDataWidth::D32);        // ...<
  vme.vmeWrite(baseAddress+NIMOSel1,
 		 (SEL_Gate1 << SEL_1)         | // Output selections...
		 (SEL_Gate2 << SEL_2)         |
		 (SEL_Gate3 << SEL_3)         |
		 (SEL_Gate4 << SEL_4), am, VMEDataWidth::D32); 
  vme.vmeWrite(baseAddress+NIMOSel2,
 		 (SEL_Gate5 << SEL_1)         | 
		 (SEL_Gate6 << SEL_2)         |
		 (SEL_Gate7 << SEL_3)         |
		 (SEL_Gate8 << SEL_4), am, VMEDataWidth::D32); 

  // Read the delays and gates.

  uint32_t delayOffset = Delay0;
  uint32_t gateOffset  = Gate0;

  for (int i=0; i < 8; i++) {
    vme.vmeRead(base() + delayOffset, (m_delays[i]), am, VMEDataWidth::D32);
    vme.vmeRead(base() + gateOffset, (m_widths[i]), am, VMEDataWidth::D32);
    
    delayOffset += 8;
    gateOffset  += 8;
  }

  
}
/*!
  set a parameter value. All values must be integers, and the parameters
  must be of one of the following forms:
  - delay[0-nchan-1]
  - width[0-nchan-1].

  
  \param parameter    Name of the parameter to modify.
  \param value        integer value to set the parameter to.

  \return std::string
  \retval ERROR - some text   - a problem was detected.
  \retval value - Success.

*/
string
CGDG::Set(const char* parameter, const char* value)
{

  unsigned int channelNumber;
  unsigned int parameterValue;

  // First decode the value as a uint32_t:

  if(sscanf(value, "%i", &parameterValue) <= 0) {
    return string("ERROR - Value is not an integer and must be");
  }                       // May need to add range checking.

  // See if the parameter is a width:n:

  if (sscanf(parameter, " width%u", &channelNumber) == 1) {
    return setWidth(channelNumber, parameterValue);
  }
  else if(sscanf(parameter, " delay%u", &channelNumber) == 1) {
    return setDelay(channelNumber, parameterValue);
  }
  else {
    return string("ERROR - parameter specifier invalid");
  }
  
}
/*!
   Get a parameter value and return it to the caller
   
   \param parameter Name of the paramter to retrieve.  Must be of the form
       - delay%u  To get a delay value. 
       - width%u  To get a width value.
   \return std::string
   \retval ERROR - yadayadya    Reports an error.
   \retval some unsigned integer Reports the value read.
*/
string
CGDG::Get(const char* parameter)
{
  unsigned int channelNumber;
  
  if(sscanf(parameter, " width%u", &channelNumber) == 1) {
    return getWidth(channelNumber);
  }
  else if (sscanf(parameter, " delay%u", &channelNumber) == 1) {
    return getDelay(channelNumber);
  }
  else {
    return string("ERROR - parameter specifier invalid");
  }
}
     
/**
 * reconfigure 
 *     The configuration (the base) has changed.  This requires us to update the
 * device:
 */
void
CGDG::reconfigure() {
  Update();
}

////////////////////////////////////////////////////////////////////////////

/*
   Retrieve the value of the base parameter.
   Note there will always be a configuration and
   nowadays we don't need to turn the -base into an integer.

*/
uint32_t 
CGDG::base()
{
  auto pConfiguration = getConfiguration();

  return pConfiguration->getIntegerParameter("-base");
  
  
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
CGDG::setDelay(unsigned int channel, unsigned int value)
{
  auto& vme(*m_pVme);                   // Reduce chances for an error.
  if (channel > 7) {
    return string ("ERROR - invalid channel");
  }
  else {
    m_delays[channel] = value;
    auto status = vme.vmeWrite(base() + Delay0 + channel*8, value, am, VMEDataWidth::D32);
    if (status) {
      stringstream smsg;
      smsg << "Error - Failed on vme write 32 for delay " << channel 
        << " : " << status.message();
      std::string msg(smsg.str());
      return msg;
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
CGDG::setWidth(unsigned int channel, unsigned int value)
{
  auto& vme(*m_pVme);
  if (channel > 7) {
    return string("ERROR - invalid channel");
  }
  else {
    m_widths[channel] = value;
    auto status = vme.vmeWrite(base() + Gate0 + channel*8, value, am, VMEDataWidth::D32);
    if (status) {
      stringstream smsg;
      smsg << "ERROR - Failed VME write32 for width " << channel 
        << " : " << status.message();
      string msg(smsg.str());
      return msg;
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
CGDG::getDelay(unsigned int channel)
{
  auto& vme(*m_pVme);
  if (channel > 7) {
    return string("ERROR - invalid channel");
  }
  else {
    auto status = vme.vmeRead(base() + Delay0 + channel*8, (m_delays[channel]), am, VMEDataWidth::D32);
			       
    if (status) {
      stringstream smsg;
      smsg << "ERROR - Vme read 32 failed for delay: " << channel
        << " : " << status.message();
      std::string message(smsg.str());
      return message;
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
CGDG::getWidth(unsigned int channel)
{
  auto& vme(*m_pVme);
  if (channel > 7) {
    return string("ERROR - invalid channel");
  }
  else {
    auto status = vme.vmeRead(base() + Gate0 + channel*8,
			       (m_widths[channel]), am, VMEDataWidth::D32);
    if (status) {
      stringstream smsg;
      smsg << "Error - vme reaVMEDataWidth::D32 failed for width " << channel <<
        " : " << status.message();
      string message(smsg.str());
      return message;
      
    }
    char msg[100];
    sprintf(msg, "%d", m_widths[channel]);
    return string(msg);
  }
}
/////////////////////////////////// GDGD Creator

/** create - create and supply the configurable parameters for a GDG */

SlowControlsDriver* GDGCreator::create(MVLC* controller) {
 SlowControlsDriver* pResult = new CGDG(controller);
 auto pConfig = pResult->getConfiguration();
 pConfig->addIntegerParameter("-base"); 

 return pResult;
}

/** Register as "jtecgdg" */

GDGCreator::Register::Register() {
  SlowControlsFactory::getInstance()->addCreator("jtecgdg", new GDGCreator);
}


static GDGCreator::Register registrar;    // try again for auto registration.