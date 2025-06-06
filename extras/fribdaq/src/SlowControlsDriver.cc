/**
 * @file SlowControlsDriver.cc
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
#include "SlowControlsDriver.h"
#include <XXUSBConfigurableObject.h>

using XXUSB::CConfigurableObject; 
using mesytec::mvlc::MVLC;

/**
 * constructor
 *    @param controller - Pointer to the MVLC controller we'll use to interact with the 
 *                   VME crate.
 *
 */
SlowControlsDriver::SlowControlsDriver(MVLC* controller) :
    m_pVme(controller), m_pConfiguration(nullptr) {
    m_pConfiguration = new CConfigurableObject("");   // We don't care about the config name.
}

/**
 * destructor
 */
SlowControlsDriver::~SlowControlsDriver() {
    delete m_pConfiguration;
}

/**
 * getConfiguration
 *    @return XXUSB::CConfigurableObject*  - pointer to our confgiuration.
 * 
 */
CConfigurableObject*
SlowControlsDriver::getConfiguration() {
    return m_pConfiguration;
}

///  Default implementations:

/**
 * reconfigure
 *     Called after a Module config command has processed all new configuration parameters.
 * Note that this is called after all configuration parameters have been processsed, for example
 * 
 * ```tcl
 * Module create v811 disriminator
 * Module config discriminator -base 0x123400 -config /some/path/to/configfile
 * ```
 * 
 * Only calls reconfigure once.  However, multiple Module commands, will call once per command.  E.g,
 * in the exmample above, if there were another Module config discriminator .... command, it would
 * invoke reconfigure again.
 */
void
SlowControlsDriver::reconfigure() {
    // By default do nothing.  For many drivers that's adequate.
}

/**
 * getMonitor
 *    If the module supports monitoring (e.g. for HV units), this will return the
 * data that can be monitored.  Note that it is up to the module to decide how this
 * is to be returned.
 * 
 * @note this is very different from VMUSBReadout where all monitorable modules contribute to a readout list
 * which is periodically triggered, data parcelled out to each driver and then this, possibly stale data are
 * returned to thge user.
 * 
 * @return  std::string - the data to return to the client.
 */
std::string
SlowControlsDriver::getMonitor() {
    return "";
}