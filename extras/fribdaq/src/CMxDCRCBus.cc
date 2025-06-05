
/*
  @file CMxDCRCBus.cc
  @brief Implement the CMxDRCBus class - control RCBus from a Mesytec digitizer.

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

#include <CMxDCRCBus.h>
#include <MADC32Registers.h>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <XXUSBConfigurableObject.h>
#include <VMEAddressModifier.h>
#include <sstream>
#include <memory>
#include <limits>
#include <cstdio>
#include <iostream>
#include <unistd.h>


// Be lazy with typing namespaced names.
using namespace std;
using mesytec::mvlc::MVLC;
using mesytec::mvlc::VMEDataWidth;
using XXUSB::CConfigurableObject;


static const uint32_t INITIAL_BASE_VALUE(0xffffffff);   //-base before configured.
/**
 * constructor
 */
CMxDCRCBus::CMxDCRCBus (MVLC* controller) :
  SlowControlsDriver(controller),
  m_maxPollAttempts(1000)
{}
/**
 *  destructor
 */
CMxDCRCBus::~CMxDCRCBus() {}



/**
 *
 */
void
CMxDCRCBus::Update() {
  activate();  
}

void
CMxDCRCBus::reconfigure() {
  activate();
}

/**
 * Set operation.  See header.
 */
std::string CMxDCRCBus::Set(const char* what, const char* value) 
{

  // TODO: Use strotoul e.g. and range check to validate

  uint16_t dataToWrite = atoi(value);        

  // Try to write until either the operation succeeds or we have exhausted
  // the allowed number of attempts
  int maxAttempts = 4, nAttempts=0;
  uint16_t response=0;
  for (; nAttempts<maxAttempts; ++nAttempts) {

    initiateWrite(what, dataToWrite);
    response = pollForResponse();

    // if we succeeded with no errors, then stop trying
    if (! responseIndicatesError(response)) {
      break;
    }

  } // end for loop
  
  // if we tried the maximum number of times without success then stop trying
  if (nAttempts==maxAttempts) {
    return convertResponseToErrorString(response);
  }

  // get the value read back from the device

  uint16_t dataRead = readResult();

  if (dataRead != dataToWrite) {
    std::stringstream smsg;
    smsg << "ERROR - CMxDCRCBus::Set - Expected to get a reply of "
      << dataToWrite << " instead got " << dataRead;
    string errmsg(smsg.str());
    
    return errmsg;
  }

  return string("OK");
}

/**
 *
 */
std::string CMxDCRCBus::Get(const char* what) 
{
  
  // Try up to 4 times to successfully complete a read transaction on RCbus
  uint16_t response=0;
  int maxAttempts=4, nAttempts=0;
  for (;nAttempts<maxAttempts; ++nAttempts) {

    initiateRead(what);
    response = pollForResponse();

    // if we succeeded with no errors, then stop trying
    if (! responseIndicatesError(response)) {
      break;
    }

  } // end for loop

  // If we failed 4 times, return ERROR and the reason why
  if (nAttempts==maxAttempts) {
    return convertResponseToErrorString(response);
  } 

  uint16_t dataRead = readResult();


  // the sluggishness of initiating stringstream is acceptable for the moment.

  stringstream retstr;
  retstr << dataRead;
  return retstr.str();

}

/**
 *
 */
void CMxDCRCBus::activate() {

  MVLC& ctlr(*m_pVme);
  uint32_t base = getConfiguration()->getUnsignedParameter("-base");
  
  auto status = ctlr.vmeWrite(base+NIMBusyFunction, 3, VMEAMod::a32UserData, VMEDataWidth::D16);
  
 

  if (status) {
    stringstream errmsg;
    errmsg << "CMxDCRCBus::activate() - executeList returned status = ";
    errmsg << status.message();

    // I've had lifetime problems just throwing stringstream::str so:

    std::string msg(errmsg.str());

    throw msg;
  }

}


/**
 * Poll for a response from the slave device:
 */
uint16_t CMxDCRCBus::pollForResponse()
{
  MVLC& ctlr(*m_pVme);
  uint32_t base = getConfiguration()->getUnsignedParameter("-base");
  
  // Poll until the device tells us to stop.
  size_t nAttempts=0;
  uint32_t data = 0;
  
  do {
    // if we timeout, throw
    if (nAttempts >= m_maxPollAttempts) {
      std::string msg("CMxDCRCBus::pollForResponse "); 
      msg += "Timed out while awaiting response";
      throw msg;
    }

    auto status = ctlr.vmeRead(base+RCStatus, data, VMEAMod::a32UserData, VMEDataWidth::D16);

    if (status) {
      stringstream errmsg;
      errmsg << "CMxDCRCBus::pollForResponse() - executeList returned status = ";
      errmsg << status.message();

      string msg(errmsg.str());
      throw msg; // failure while communicating with VM-USB is worthy 
                          // of a thrown exception
    }
      
    ++nAttempts;
  } while ( (data & RCSTAT_MASK) != RCSTAT_ACTIVE );

  return data;
}


/**
 *
 */
uint16_t CMxDCRCBus::readResult() 
{
  MVLC& ctlr(*m_pVme);
  uint32_t base     = getConfiguration()->getUnsignedParameter("-base");
  uint32_t dataRead = 0;

  auto status = ctlr.vmeRead(base+RCData, dataRead, VMEAMod::a32UserData, VMEDataWidth::D16); 
  if (status) {
    stringstream errmsg;
    errmsg << "ERROR - ";
    errmsg << "CMxDCRCBus::readResult - failure executing list with ";
    errmsg << "status = " << status.message();

    string msg(errmsg.str());

    throw msg; // failure while communicating with VM-USB is worthy 
                        // of a thrown exception
  }

  return dataRead;
}

/**
 *  Turn an adress string into a pair of <devno,adress>
 */
std::pair<uint16_t,uint16_t> 
CMxDCRCBus::parseAddress(std::string what)
{
  uint16_t devNo=0;
  uint16_t addr=0;
  int ntokens = sscanf(what.c_str(), "d%hua%hu", &devNo, &addr);
  if (ntokens!=2) {
    throw string("CMxDCRCBus::parseAddress Failed to parse address string");
  }

  return make_pair(devNo, addr);
}


/**
 *
 */
bool CMxDCRCBus::responseIndicatesError(uint16_t datum)
{
  return (((datum & RCSTAT_ADDRCOLLISION) | (datum & RCSTAT_NORESPONSE))!=0);
}

/**
 *
 */
std::string CMxDCRCBus::convertResponseToErrorString(uint16_t datum) 
{

  string message;

  // at the moment there are only two error flags
  if (datum & RCSTAT_ADDRCOLLISION) {
    message = "ERROR - Address collision during last RC-bus operation";  
    message += " : code=";
    message += to_string(datum);
  } else if (datum & RCSTAT_NORESPONSE) {
    message = "ERROR - No response during last RC-bus operation"; 
    message += " : code=";
    message += to_string(datum);
  } else {
    message = "ERROR - Unknown error code returned from last RC-bus operation";
    message += " : code=";
    message += to_string(datum);
  }

  return message;
}


 
void CMxDCRCBus::addParameterWrite(
                                   std::pair<uint16_t,uint16_t> addresses,
                                   uint16_t value)
{
  MVLC&  list(*m_pVme);    // Lazy I know.
  uint32_t base = getConfiguration()->getUnsignedParameter("-base");
  auto status = list.vmeWrite(base+RCModNum, addresses.first, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME write failed: ", " writing RCModNum");

  status = list.vmeWrite(base+RCOpCode, RCOP_WRITEDATA, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME write failed : ", " writing Write data opcode");

  list.vmeWrite(base+RCAddr, addresses.second,  VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME write failed : ", " writing address within module");

  list.vmeWrite(base+RCData,  value, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME write failed : ", " writing data value");
}

/**H
 *
 */
void CMxDCRCBus::addParameterRead(
                                   std::pair<uint16_t,uint16_t> addresses)
{
  MVLC& list(*m_pVme);
  uint32_t base = getConfiguration()->getUnsignedParameter("-base");
  std::error_code status;

  status = list.vmeWrite(base+RCModNum,  addresses.first, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME Write failed: ", " writing module number");

  list.vmeWrite(base+RCOpCode, RCOP_READDATA, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME Write failed: ", " writing Read data opcode");

  list.vmeWrite(base+RCAddr,  addresses.second, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME Write failed: ", " writing Read address within module ");

  list.vmeWrite(base+RCData,   0, VMEAMod::a32UserData, VMEDataWidth::D16);
  throwIfBadStatus(status, "ERROR - VME Write failed: ", " writing dumy data for read");
}


void CMxDCRCBus::initiateWrite(
                               std::string what,
                               uint16_t value)
{
  // extract the device address and parameter address encoded in "what" argument
  pair<uint16_t,uint16_t> busAddress = parseAddress(what);

  addParameterWrite(busAddress, value);

}

void CMxDCRCBus::initiateRead(
                               std::string what)
{
  // extract the device address and parameter address encoded in "what" argument
  pair<uint16_t,uint16_t> busAddress = parseAddress(what);

  // Create a list and append the ensemble of commands for a parameter write 
  // to it.
  addParameterRead(busAddress);

}

/**
 *  throwIfBadStatus
 *       factored out error checking; see header.
 */
void
CMxDCRCBus::throwIfBadStatus(
  const std::error_code& status,
  const char* prefix, const char* suffix
) {
  if (status) {
    stringstream smsg;
    smsg << prefix <<  status.message() << suffix;
    string msg(smsg.str());
    throw msg;
  }
}

////////////////////////////////// Implementation of MxDCBusCreator and auto registration.

/**
 *  create
 *     Create a driver instance.
 * 
 * @param controller - MVLC controller used by the driver to perform operations.
 * @return SlowControlsDriver* - pointer to the dynamically instantiate driver.
 */
SlowControlsDriver*
MxDCBusCreator::create(MVLC* controller) {
  SlowControlsDriver* result = new CMxDCRCBus(controller);

  // Define the configurable parameters:

  result->getConfiguration()->addIntegerParameter("-base", INITIAL_BASE_VALUE);

  return result;
}

/** Auto registration: */

MxDCBusCreator::Register::Register() {
  SlowControlsFactory::getInstance()->addCreator("mxdcrcbus", new MxDCBusCreator);
}


static MxDCBusCreator::Register registrar;