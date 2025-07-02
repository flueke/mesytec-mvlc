/*
   @file CMxDCReset.h
   @brief define a class to do a soft reset of an RC bus on e.g. MADC32.
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

#ifndef MVLC_CMXDCRESET_H
#define MVLC_CMXDCRESET_H

#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <string> 

// Forward class definitions:

namespace mesytec {
  namespace mvlc {
    class MVLC;
  }
}

class CVMUSB;

///////////////////////////////////////////////////////////////////////////////
//////////////////////// SLOW CONTROLS FOR SOFT RESETS ////////////////////////
///////////////////////////////////////////////////////////////////////////////

/**! Slow control support for resetting any of the MxDC family of digitizers
*
* Implementation of a CControlHardware derived class to be used through
* the Module command. There is a corresponding CMxDCResetCreator class
* that accompanies this.
*
* This only provides the ability to perform a soft reset of any Mesytec MxDC device
* via the reconfigure method. All other methods are no-ops and simply return an
* error indicating that they do not have have implementation.
*
*  The only configuration parameter is -base.
*
*/
class CMxDCReset : public SlowControlsDriver
{
  public:
    CMxDCReset(mesytec::mvlc::MVLC* controller);


  
    virtual void reconfigure();
    virtual void Update();
    virtual std::string Set(const char* what, const char* value);
    virtual std::string Get(const char* what);

  };
/**
 * @class MxDCResetCreator
 *    Calling MxDCCreator::Register's constructor registers us as
 * a module cretaor for "mxdcreset",
 */
class MxDCResetCreator : public SlowControlsCreator {
public:  
  SlowControlsDriver* create(mesytec::mvlc::MVLC* controller);
  class Register {
  public:
    Register();
  };
};
#endif
