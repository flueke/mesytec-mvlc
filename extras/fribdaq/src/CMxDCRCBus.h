
/*
  @file CMxDCRCBus.h
  @brief Define the CMxDRCBus class - control RCBus from a Mesytec digitizer.

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


#ifndef MVLC_CMXDCRCBUS_H
#define MVLC_CMXDCRCBUS_H
#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <string>
#include <cstdint>
#include <utility>
#include <system_error>

namespace mesytec {
  namespace mvlc {
    class MVLC;
  }
}



/**
 * @class CMxDRCBus
 *    Several of the mesytec devices e.g. MADC32 have the ability to 
 * program a busy output as an RC bus master.  This class supports
 * doing that, and provides remote control of that RC bus.
 * The RC bus is a Mesytec proprietary daisy chained communications
 * bus for their slow controls devices - e.g. the MSCF-16 shaping amplfier
 * has an RC bus input.
 * The only configuration parameter is: -base the base of the digitizer
 * we'll be using as the RCBus conttroller.
 */
class CMxDCRCBus : public SlowControlsDriver
{
  private:
    size_t m_maxPollAttempts;

  public:
    CMxDCRCBus(mesytec::mvlc::MVLC* controller);
    virtual ~CMxDCRCBus();
private:
    CMxDCRCBus(const CMxDCRCBus& rhs);
    CMxDCRCBus& operator=(const CMxDCRCBus& rhs);
    int operator==(const CMxDCRCBus&);
    int operator!=(const CMxDCRCBus&);
    
  // Operations:
public:
    /**! Setup the MxDC for RC-bus operations
     *
     *  The MxDC module's NIM Busy ouput is hijacked to be used for the 
     *  RC-bus control.
     *
     *  \param ctlr the controller to communicate through
     */
    virtual void reconfigure();

    /**! Does nothing but return "OK" */
    virtual void Update();

    /**! Write a value to a parameter address 
     *
     *  This attempts to send a write command to the device on the RC-bus. The 
     *  particular device and parameter address are identified by the what 
     *  argument. See the parseAddress method for the format accepted. It is not
     *  uncommon that the write will fail the first time so this will actually 
     *  attempt up to 4 times to reach a successful transmission. If it fails
     *  all 4 times, then an error response prefixed by "ERROR - " is returned
     *  containing an indication of what happened. Otherwise, if the
     *  transmission succeeded, the device will read back the value and check
     *  that it is the same as was written. If it differs, then an error
     *  response starting with "ERROR - " is returned indicating so.
     *
     *  \param ctlr   controller to communicate through
     *  \param what   address specifier (eg. d4a23 --> device 4, address 23)
     *  \param value  string representation of the value to write
     *
     *  \returns string indicating success or failure
     *  \retval "OK"          - success
     *  \retval "ERROR - ..." - for any failure on the RC-bus operations
     *  \throws string if communication with the VM-USB returns nonzero status
     */
    virtual std::string Set(const char* what, const char* value);

    /**! Read a value from a parameter address
     *
     * The behavior of this is similar to the Set method. It attempts 4 times to
     * read a value from an address in the specified device. If it succeeds
     * without error, the value is returned. Otherwise, if after 4 attempts it
     * has still not succeeded, the device will be returned as zero.
     *
     * \param ctlr    controller to communicate through
     * \param what    address specifier (eg. d4a23 --> device 4, address 23)
     *
     * \returns string indicating success or failure
     * \retval "OK"           - success
     *  \retval "ERROR - ..." - for any failure on the RC-bus operations
     *  \throws string if communication with the VM-USB returns nonzero status
     */
    virtual std::string Get(const char* what);


    // Getters and setters over the number of allowed poll attempts
    size_t getPollTimeout() const { return m_maxPollAttempts;}
    void   setPollTimeout(size_t value) { m_maxPollAttempts = value; }

  private:
    ///////////////////////////////////////////////////////////////////////////
    // UTILITY methods
    //

    /**! Enlist NIM Busy output on MxDC for RC-bus operations
     *
     *  \param ctlr   controller to communicate through 
     *
     *  \throws string if communicate with the VM-USB returns nonzero status
     */
    void activate();


    /**! Poll the 0x608A address until bit0 is 0
     *
     * This is called after an initiateWrite or initiateRead method to
     * wait for the device's error response.
     * 
     * \param ctlr    controller to communicate through 
     *
     * \return value read from 0x608A once bit0 becomes 0
     *
     * \throws string if communication with the VM-USB returns nonzero status
     */
    uint16_t pollForResponse(); 

    /**! Read the resulting data response at 0x6088
     *
     * \param ctlr    controller to communicate through
     *
     * \return value read from 0x6088
     * \throws string if communication with the VM-USB returns nonzero status
     */
    uint16_t readResult();

    /**! Set up the commands for a write and then put value on the wire
     *
     * \param ctlr    controller to communicate through
     * \param what    parameter address (e.g. d2a42 --> device 2, address 42)
     * \param value   value to write
     *
     * \throws string if communication with the VM-USB returns nonzero status
     */
    void initiateWrite(std::string what, uint16_t value);

    /**! Set up the commands for a read and then execute the action
     *
     * \param ctlr    controller to communicate through
     * \param what    parameter address (e.g. d2a42 --> device 2, address 42)
     *
     * \throws string if communication with the VM-USB returns nonzero status
     */
    void initiateRead(std::string what);

    /**! Parse the string encoded with device number and parameter address
     *
     * The "what" parameter in the Get and Set methods contains both the device
     * number and the parameter address. The format is quite simple and is just:
     * dXaY, where X is the device number and Y is the parameter address. In 
     * RC-bus lingo the device number sometime referred to as a device address.
     * 
     * \param what    string of form dXaY where X=device number, Y=param address
     * \returns  pair with first=X, second=Y
     *
     * \throws string if unable to parse the format
     */
    std::pair<uint16_t,uint16_t> parseAddress(std::string what);

    /**! Add the commands for initiating a write to a readout list
     *
     * \param list  a readout list
     * \param addr  the parameter address struct returned from parseAddresses
     * \param value the value to write
     *  @note with the MVLC we just do the commands immediately.
     */
    void addParameterWrite( 
                           std::pair<uint16_t,uint16_t> addr,
                           uint16_t value);

    /**! Add the commands for initiating a read to a readout list
     *
     * \param list  a readout list
     * \param addr  the parameter address struct returned from parseAddresses
     *  @note - with the mvlc we just do the commands immediately 
     * 
     * TODO: Signature will need to change to return the read data somehow.
     */
    void addParameterRead(std::pair<uint16_t,uint16_t> addr);

    /**! Check whether error bits are set
     *
     * This is called to evaluate whether a transaction succeeded or ended
     * erroneously with a known error.
     *
     * \param datum  the word to check
     *
     * \retval true  - an error bit was set in datum
     * \retval false - no error bits were set 
     *
     */
    bool responseIndicatesError(uint16_t datum);

    /**! Return a string specifying what error occurred
     *
     * Checks the error bits to determine what type of error occurred. 
     * It is assumed that the user has already verified that an error bit is
     * set.
     *
     * \param datum   response word with error bits set
     *
     * \return string providing reason for error bit
     */
    std::string convertResponseToErrorString(uint16_t datum);

    /** 
     * throw a string exception if the std::error_code is an error.
     * 
     * @praam status - the error code.
     * @param prefix - prefix text.
     * @param suffix - Suffix text.
     * 
     * Example:
     * ```c++
     *   throwIfBadStatus(status, "ERROR - Executing a VME write: ", " RCModNum write.")
     * ```
     */
    void throwIfBadStatus(
      const std::error_code& status, 
      const char* prefix, const char* suffix
    );
};

/**
 *  @class MxDCBusCreator 
 *    Creator for the MxDRCBus slow controls driver.  You can register it
 * with the factory by instanciating an MxDCBusCreator::Register object.
 * The CMxDCRBus.cc file has a line like:
 * ```c++
 * static MxDCBusCreator::Regsiter register;
 * ```
 * 
 * Which auto registers the creator.
 */
class MxDCBusCreator : public SlowControlsCreator {
public:
  SlowControlsDriver* create(mesytec::mvlc::MVLC* controller);
  class Register {
  public:
    Register();                           // Construction registers the creator.
  };
};

#endif
