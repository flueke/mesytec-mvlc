/**
 * @file CVMEClientd.h
 * @brief Defines the Client side of the vme slow controls driver.
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
#ifndef MVLC_CVMECLIENT_H
#define MVLC_CVMECLIENT_H

#include <string>
#include <vector>
#include <stdint.h>

/**
 * @class CVMEClient
 *   Provides a client for the vme/vmusb slow controls
 * driver.  The client class allows the user to
 * - Specify a list of operations to be performed on the remote.
 * - Execute those opreations resulting in a vector of read data.
 * - Given a specific operation in the list, determine the index in which
 * data it read will be located in the read data
 * - Clear the list rinse and repeat.
 * 
 * @note:  Each list execution forms and dissolves a connection with
 * the slow controls server.   This allows the client to work across
 * execution instances of the program running the server.
 * 
 * TODO: use a persistent interpreter.
 */
class CVMEClient {
// Public class types:

public:
    enum class DataWidth {D16, D32};       // D8 not allowed
// object data:

private:
    std::string m_host;
    std::string m_name;
    uint16_t    m_port;

    std::vector<std::string> m_operations;
    std::vector<int>         m_readIndices;
    int                      m_nextReadIndex;

    // Canonicals:
public:
    CVMEClient(const char* host, const char* moduleName, uint16_t port);
    virtual ~CVMEClient();             // Not final.

    // Disallowed canonicals.

private:
    CVMEClient(const CVMEClient&);
    CVMEClient& operator=(const CVMEClient&);
    int operator==(const CVMEClient&) const;
    int operator!=(const CVMEClient&) const;

    // VME list operations:
public:
    int  addRead(uint32_t addr, uint8_t amod, DataWidth width);
    void addWrite(
        uint32_t addr, uint8_t amod, uint32_t data, DataWidth width
    );
    std::vector<uint32_t> execute();
    int readIndex(size_t operationIndex);

    void reset();

    // private utilities:

private:
    std::string buildRequest();
    std::string transact(const std::string& request);
    std::vector<uint32_t> distributeData(const std::string& reply);
    std::string TclRuntimeMessage();

};
#endif