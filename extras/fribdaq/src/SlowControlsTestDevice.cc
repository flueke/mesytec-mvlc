/**
 * @file SlowControlsTestDevice.cc
 * @brief Implementation of the slow controls test device.
 * @author Ron Fox <fox @ frib dot msu dot edu>
 *
 *
 *   This software is Copyright by the Board of Trustees of Michigan
 *   State University (c) Copyright 2025.
 *
 *   You may use this software under the terms of the GNU public license
 *   (GPL).  The terms of this license are described at:
 *
 *    http://www.gnu.org/licenses/gpl.txt
 *
 * @note - this file is private to fribdaq's readout and won't be installed.
 */
#include "SlowControlsTestDevice.h"
#include <XXUSBConfigurableObject.h>
#include <sstream>
#include <stdlib.h>   // for strtol.
#include <tcl.h>

using XXUSB::CConfigurableObject;
using mesytec::mvlc::MVLC;


 /////////////////////////// Implement our creator

 /** create
  *      create a Test device and define its configuration.
  */
 SlowControlsDriver*
 SlowControlsTestCreator::create(MVLC* controller) {
    auto result = new SlowControlsTestDriver(controller);

    CConfigurableObject* pConfig = result->getConfiguration();
    pConfig->addIntegerParameter("-parameter", 0, 65535, 0);   // 16 bit integer value.

    return result;
 }

 ///////////////////////// Implement the test driver:

 /** constructor
  * 
  * @param vme - controller that can talk to the VME
  */
SlowControlsTestDriver::SlowControlsTestDriver(MVLC* vme) :
    SlowControlsDriver(vme), m_readwrite(0) {}

/** destructor */

SlowControlsTestDriver::~SlowControlsTestDriver() {}

/**
 * Set 
 *    Set parameter
 * 
 * @param pv - name of the parameter.
 * @param value - value of the parameter (textual);
 * 
 * Possible errors:
 *    - pv is not 'readswrite'
 *    - value is not an integer.
 */
std::string
SlowControlsTestDriver::Set(const char* pv, const char* value) {
    std::string param(pv);
    if (param != "readwrite") {
        std::stringstream smsg;
        smsg << "ERROR - there is no writable parameter named : " << param;
        std::string s(smsg.str());
        return s;
    }
    char* endptr;
    long lvalue = strtol(value, &endptr, 0);     // base 0 allows e.g. 0xnnn
    if ((lvalue == 0) && (value == endptr)) {
        std::stringstream smsg;
        smsg << "ERROR - " << value << " Is not a valid value for " << pv;
        std::string s(smsg.str());
        return s;
    }

    m_readwrite = lvalue;
    return std::string("OK");
}
/**
 *  Get 
 *    return the value of a parameter or error:
 * 
 * @param pv - name of the parameter to get.
 * @return std::string value gotten.
 * 
 * 
 */
std::string
SlowControlsTestDriver::Get(const char* pv) {
    std::string param(pv);
    std::stringstream r;

    if (param == "configured") {
        r << getConfiguration()->cget("-parameter");
    } else if (param == "fixed") {
        r << "0xaaaa";
    } else if (param == "readwrite") {
        r << m_readwrite;
    } else {
        r << "ERROR - " << pv << " Is not a valid parameter for test devices";
    }

    std::string result(r.str());
    return result;
}
/**
 * getMonitor
 *    Return the a dict with keys, the parameter names, and values their values.
 */
std::string
SlowControlsTestDriver::getMonitor() {
    std::string result;
    auto pInterp = Tcl_CreateInterp();    // Needed to build the dict.

    Tcl_Obj* pDict = Tcl_NewDictObj();
    AddDictItem(pInterp, pDict, "configured", getConfiguration()->getIntegerParameter("-parameter"));
    AddDictItem(pInterp, pDict, "fixed", 0xaaaa);
    AddDictItem(pInterp, pDict, "readwrite", m_readwrite);

    int l;
    result = Tcl_GetStringFromObj(pDict, &l);
    Tcl_DecrRefCount(pDict);                // Force deletion -- I think.
    Tcl_DeleteInterp(pInterp);

    return result;
}

/**
 *  AddDictItem
 *    Add an integer item to a dict object:
 * 
 * @param pInterp - Tcl_Interp*
 * @param dict    - dict obj.
 * @param key     - key to add/replace.
 * @param value   - intger value to associate with the key:
 */
void
SlowControlsTestDriver::AddDictItem(
    Tcl_Interp* pInterp, Tcl_Obj* dict, const char* key, int value
) {
    Tcl_Obj* keyobj = Tcl_NewStringObj(key, -1);
    Tcl_Obj* vobj   = Tcl_NewIntObj(value);

    Tcl_DictObjPut(pInterp, dict, keyobj, vobj);
}

class RegisterTestCreator {
public:
    RegisterTestCreator() {
        SlowControlsFactory::getInstance()->addCreator("test", new SlowControlsTestCreator);
    }
};

static RegisterTestCreator reg;