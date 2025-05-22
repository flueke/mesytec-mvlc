/*
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

#ifndef MVLC_CGDG_H
#define MVLC_CGDG_H
#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <string>
#include <stdint.h>

namespace mesytect {
  namespace mvlc {
    class MVLC;
  }
}
namespace XXUSB {
  class CConfigurableObject;
}


/*!
   CGDG controls a JTEC/Wiener Gate and delay generator. This is an 8 channel
   unit.  At this time we only know of the following parameters:
   - delay$n$  (n = 0 through 7).  The delay for channel n.
   - width$n$  (n = 0 through 7).  The output width for channel n

   The configuration parameters are just:
   - base  - The base address of the module. We assume that address modifiers
             will be extended user data.

  We don't support monitoring.
*/
class CGDG : public SlowControlsDriver
{
private:

  mesytec::mvlc::MVLC*             m_pController;

  uint32_t                  m_delays[8];
  uint32_t                  m_widths[8];

public:
  // Cannonical operations:

  CGDG(mesytec::mvlc::MVLC* vme);
  virtual ~CGDG();

  // forbidden canonicals.
private:
  CGDG(const CGDG&);
  CGDG& operator=(const CGDG& rhs);
  int operator==(const CGDG& rhs) const;
  int operator!=(const CGDG& rhs) const;

  // virtual overrides:

public:
 
  virtual void Update();             
  virtual std::string Set(const char* parameter, const char* value); 
  virtual std::string Get(const char* parameter);
  virtual void reconfigure();

private:
  uint32_t base();
  std::string setDelay(unsigned int channel, unsigned int value);
  std::string setWidth(unsigned int channel, unsigned int value);

  std::string getDelay(unsigned int channel);
  std::string getWidth(unsigned int channel);
};

/**
 *  @class CGDGCreator 
 *    Creator to register in the Slow controls factory as jtecgdg
 * with the -base integer parameter.
 */
class GDGCreator : SlowControlsCreator {
public:
  virtual SlowControlsDriver* create(mesytec::mvlc::MVLC* controller);
  class Register {
  public:
    Register();
  };
};
#endif
