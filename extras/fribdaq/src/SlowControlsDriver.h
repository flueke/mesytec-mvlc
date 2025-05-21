/**
 * @file SlowControlsDriver.h
 * @brief Base class for a slow controls driver.
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
#ifndef _MVLC_SLOWCONTROLSDRIVER_H
#define _MVLC_SLOWCONTROLSDRIVER_H
#include <string>
namespace mesytec {
    namespace mvlc {
        class MVLC;
    }
}
namespace XXUSB {
    class CConfigurableObject;
}

/**
 * @class SlowControlsDriver
 * 
 * This is an abstract base class for all slow controls drivers.  It provides
 * pure virtual entry points for the Set and Get and Monitor operations.
 * 
 * 
 * To use this, you'll need to derive a concrete class from this that:
 * - Gets its configuration from its associated Module command handler.
 * - Implements concrete  Set, Get and Update operations.
 * - If the device supports monitoring, it can implement the getMonitor operation
 * to return currently monitored data.
 * 
 * This base class also encapsulates a configurable object that can be
 * gotten by the module command to configure the device (e.g. base address).
 * The optional reconfigure operation is called after the configuration has been modified.
 * This can be implemented or not as the driver requires (e.g. a configuration might
 * be for a file containing device settings when the reconfigure operation woule rewrite
 * the device settings.)
 * 
 * @note One key difference between the MVLC and the VMUSB - the VMUSB has a distinct 
 * data taking (autonomous mode), which enables the device to respond to stack triggers.
 * While in this mode, individual operations are not allowed.  The MVLC, however treats
 * individual operations like a stack which can be programmatically triggered at any time.
 * Thus, rather than having devices contribute to a monitor stack which is periodically 
 * triggered by software whose data are then distributed to the approrpiate module driver 
 * instances, the MVLC monitoring can just perform the operations on demand and resturn 
 * the associated data.   This simplifies the entire monitor infrastructure.
 * 
 * @note while the configurable parameters must be agreed upon between the module command and
 *   the module driver, it is the module driver that defines them after it creates a new
 *   module.
 */
class SlowControlsDriver {
    // Data available to derived classes.

protected: 
    mesytec::mvlc::MVLC*        m_pVme;

    // Private since there's a selector.
private:
    XXUSB::CConfigurableObject* m_pConfiguration;

    // Exported canonicals:
public:
    SlowControlsDriver(mesytec::mvlc::MVLC* vme);
    virtual ~SlowControlsDriver();

    // Disallowed canonicals:
private:
    SlowControlsDriver(const SlowControlsDriver&);            // Copy construction.
    SlowControlsDriver& operator=(const SlowControlsDriver&); // assignment.
    int operator==(const SlowControlsDriver& );               // Equality compare.
    int operator!=(const SlowControlsDriver&);                // inequality compare.

    // Selectors:
public:
    XXUSB::CConfigurableObject* getConfiguration();

    // Operations that define the module:

public:
    virtual std::string Set(const char* pv, const char* value) = 0;
    virtual std::string Get(const char* pv) = 0;
    virtual void Update() = 0;
    virtual void reconfigure();
    virtual std::string getMonitor();
};
#endif