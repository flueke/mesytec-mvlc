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

#ifndef __CGDG_H
#define __CGDG_H
#ifndef __CCONTROLHARDWARE_H
#include "CControlHardware.h"
#endif

#ifndef __STL_STRING
#include <string>
#ifndef __STL_STRING
#define __STL_STRING
#endif
#endif

#ifndef __CRT_STDINT_H
#include <stdint.h>
#ifndef __CRT_STDINT_H
#define __CRT_STDINT_H
#endif
#endif


#include <CControlModule.h>
class CVMUSB;


/*!
   CGDG controls a JTEC/Wiener Gate and delay generator. This is an 8 channel
   unit.  At this time we only know of the following parameters:
   - delay$n$  (n = 0 through 7).  The delay for channel n.
   - width$n$  (n = 0 through 7).  The output width for channel n

   The configuration parameters are just:
   - base  - The base address of the module. We assume that address modifiers
             will be extended user data.
*/
class CGDG : public CControlHardware
{
private:
  CControlModule*      m_pConfiguration;

  uint32_t                  m_delays[8];
  uint32_t                  m_widths[8];

public:
  // Cannonical operations:

  CGDG();
  CGDG(const CGDG& rhs);
  virtual ~CGDG();
  CGDG& operator=(const CGDG& rhs);
  int operator==(const CGDG& rhs) const;
  int operator!=(const CGDG& rhs) const;

  // virtual overrides:

public:
  virtual void onAttach(CControlModule& configuration);  //!< Create config.
  virtual void Initialize(CVMUSB& vme);
  virtual std::string Update(CVMUSB& vme);               //!< Update module.
  virtual std::string Set(CVMUSB& vme, 
			  std::string parameter, 
			  std::string value);            //!< Set parameter value
  virtual std::string Get(CVMUSB& vme, 
			  std::string parameter);        //!< Get parameter value.
  virtual CControlHardware* clone() const;	     //!< Virtual copy constr.

private:
  uint32_t base();
  std::string setDelay(CVMUSB& vme, unsigned int channel, unsigned int value);
  std::string setWidth(CVMUSB& vme, unsigned int channel, unsigned int value);

  std::string getDelay(CVMUSB& vme, unsigned int channel);
  std::string getWidth(CVMUSB& vme, unsigned int channel);
};
#endif
