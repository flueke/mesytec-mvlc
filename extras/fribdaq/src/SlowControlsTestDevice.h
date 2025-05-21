/**
 * @file SlowControlsTestDevice.h
 * @brief Header for the slow controls test device.
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
#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <tcl.h>

namespace mesytec {
    namespace mvlc {
        class MVLC;
    }
}

/**
 * @class SlowControlsTestDriver
 *     Stupid test driver.  We suport the following gettable parameters:
 * 
 * - configured - the value of the -parameter config parameter.
 * - fixed      - some fixed value ...0xaaaa.
 * - readwrite  - a parameter that can also be Set returns the last Set value.
 * 
 * All are considered monitord and getMonitor returns a dict with all the values.
 * 
 */
class SlowControlsTestDriver : public SlowControlsDriver {
    // data:
private:
    int m_readwrite;  // value of the read/write parameter.
public:
    SlowControlsTestDriver(mesytec::mvlc::MVLC* vme);
    virtual ~SlowControlsTestDriver();
private:
    SlowControlsTestDriver(const SlowControlsTestDriver&);
    SlowControlsTestDriver& operator=(const SlowControlsTestDriver&);
    int operator==(const SlowControlsTestDriver&);
    int operator!=(const SlowControlsTestDriver&);

    // operations:

public:
   virtual std::string Set(const char* pv, const char* value) ;
   virtual std::string Get(const char* pv) ;
   virtual std::string getMonitor();
   virtual void Update() {}
private:
    void AddDictItem(Tcl_Interp* pInterp, Tcl_Obj* dict,const char* key, int value);
};

/**
 * @class SlowControlsTestCreator
 *    Class derived from SlowControlsCreator to create a new 'test' device.
 */
class SlowControlsTestCreator : public SlowControlsCreator {
public:
    virtual SlowControlsDriver* create(mesytec::mvlc::MVLC* controller);
};