/*
   @file CMxDCReset.vv
   @brief Implement a class to do a soft reset of an RC bus on e.g. MADC32.
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2025.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

#include <CMxDCReset.h>
#include <MADC32Registers.h>
#include <VMEAddressModifier.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <XXUSBConfigurableObject.h>
#include <unistd.h>

#include <stdint.h>


using mesytec::mvlc::MVLC;
using mesytec::mvlc::VMEDataWidth;


static const uint32_t INITIAL_BASE_VALUE(0xffffffff);

CMxDCReset::CMxDCReset(MVLC* controller) :
  SlowControlsDriver(controller)
{}


void CMxDCReset::reconfigure()
{

  uint32_t base = getConfiguration()->getUnsignedParameter("-base");
  if (base != INITIAL_BASE_VALUE) {
    MVLC& controller(*m_pVme);
    std::cout << "Resetting the MxDC device at base 0x";
    std::cout << std::hex << base << std::dec << std::endl;
    // reset counters ctra and ctrb
    controller.vmeWrite(base+TimestampReset, 0x3, VMEAMod::a32UserData, VMEDataWidth::D16);
    // soft reset
    controller.vmeWrite(base+Reset, 0, VMEAMod::a32UserData, VMEDataWidth::D16);
    // give time for the reset to take effect
    sleep(1);
  }
}

void CMxDCReset::Update()
{
  reconfigure();
}

std::string CMxDCReset::Set(const char* what, const char* val) 
{
  return "ERROR - Set is not implemented for CMxDCReset";
}

std::string CMxDCReset::Get(const char* what)
{
  return "ERROR - Get is not implemented for CMxDCReset";
}

////////////////////////////////// implement the crator and auto registration.

/**
 * create 
 *    Create a new CMxDCReset driver and it's configuration definition.
 */
SlowControlsDriver*
MxDCResetCreator::create(MVLC* controller) {
  SlowControlsDriver* result = new CMxDCReset(controller);

  result->getConfiguration()->addIntegerParameter("-base", INITIAL_BASE_VALUE);

  return result;
}

// AUto registration:


MxDCResetCreator::Register::Register() {
  SlowControlsFactory::getInstance()->addCreator("mxdcreset", new MxDCResetCreator);
}


static MxDCResetCreator::Register reg;