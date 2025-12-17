/**
 * @file CVMEClient.cc
 * @brief Implements the Client side of the vme slow controls driver.
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
#include "CVMEClient.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <tcl.h>
#include <sstream>
#include <stdexcept>
#include <limits.h>

/**
 * constructor:
 *    @param host - name of the system running the server.
 *    @param moduleName - name of the controller created by the 'Module' command.
 *    @param port - Port on which the server is listening for connections.
 * 
 */
CVMEClient::CVMEClient(const char* host, const char* moduleName, uint16_t port) :
    m_host(host), m_port(port), m_name(moduleName), m_nextReadIndex(0) {}

/**
 * destructor - proovided in case subclasses need to do something
 */
CVMEClient::~CVMEClient() {}


/**
 * addRead
 *    Add a read to the list of operations that will be requested of
 * the server at the next execute invocation.
 * 
 * @param addr - Address to read.
 * @param amod - Address modifier for the read.
 * @param width - Width of the read.
 * @return  The  read index of the data that will be returned for this
 *  read from the execute method.
 */
int
CVMEClient::addRead(uint32_t addr, uint8_t amod, DataWidth width) {
    // encode the read:

    std::stringstream encoder;
    encoder << std::hex << "r 0x" << unsigned(amod) << " 0x" << addr << " " 
	    << ((width == DataWidth::D16) ? "16" : "32");
    std::string encoded(encoder.str());

    // Save it and its read index.

    m_operations.push_back(encoded);
    int readindex = m_nextReadIndex++;
    m_readIndices.push_back(readindex);

    return readindex;
}
/**
 * addWrite
 *    Add a write operation to the list of operations that will be requested
 * by the next invocation of execute().   
 * 
 * @note -1 will be saved as the read index for this operation to flag
 * it does not have read data.  
 * 
 * @param addr - address to write to.
 * @param amod - address mdifier to use in the operation.
 * @param data - Data to write.
 * @param width - width of the operation.
 */
void
CVMEClient::addWrite(
uint32_t addr, uint8_t amod, uint32_t data, DataWidth width
) {
    // Encode the operation:

    std::stringstream encoder;
    encoder << std::hex << "w 0x" << unsigned(amod) << " 0x" << addr << " 0x" << data
	    << " " << ((width == DataWidth::D16) ? "16" : "32");

    std::string encoded(encoder.str());

    // Save the operation for later:

    m_operations.push_back(encoded);
    m_readIndices.push_back(-1);   // Reserve space for the read index.
}
/**
 * addBlockRead
 *   Add a block read operation to the list of operations that will be
 * requested by the next invocation of execute().   The format of a block read
 * request is "rb 0xamod 0xaddress count".  Read widths are always
 * 32 bits.
 */
void
CVMEClient::addBlockRead(uint32_t addr, uint16_t maxRead, uint8_t amod) {
    std::stringstream encoder;
    encoder << std::hex << "rb 0x" << unsigned(amod) << " 0x" << addr << " " << maxRead;
    std::string encoded(encoder.str());

    m_operations.push_back(encoded);
    m_readIndices.push_back(m_nextReadIndex++);
}
/**
 * addFifoRead
 *   Add a fifo read operation to the list of operations that will be
 * requested by the next invocation of execute().   The format of a fifo read
 * request is "rf 0xamod 0xaddress count".  Read widths are always
 * 32 bits.
 */
void
CVMEClient::addFifoRead(uint32_t addr, uint16_t maxRead, uint8_t amod) {
    std::stringstream encoder;
    encoder << std::hex << "rf 0x" << unsigned(amod) << " 0x" << addr << " " << maxRead;
    std::string encoded(encoder.str());

    m_operations.push_back(encoded);
    m_readIndices.push_back(m_nextReadIndex++);
}
/**
 * addBlockWrite
 *   Add a block write operation to the list of operations that will be
 * requested by the next invocation of execute().   The format of a block write
 * request is "wb 0xamod 0xaddress width {0xdata1 0xdata2 ...}".  Write widths are always
 * 32 bits.
 * 
 * @param addr - address to write to.
 * @param amod - address modifier to use.
 * @param data - vector of data to write.
 * @param width - width of the write (D16 or D32).
 */
void
CVMEClient::addBlockWrite(
    uint32_t addr, uint8_t amod, 
    const std::vector<uint32_t>& data, DataWidth width) {
    std::stringstream encoder;
    encoder << std::hex << "wb 0x" << unsigned(amod) << " 0x" << addr << " "
        << (width == DataWidth::D16 ? "16" : "32")
        << " {" << std::dec;

    for (auto item : data) {
        encoder << " " << item;
    }
    encoder << " }";
    std::string encoded(encoder.str());
    m_operations.push_back(encoded);
    m_readIndices.push_back(-1);   // Reserve space for the read index.
}
/**
 * addFifoWrite
 *  Add a fifo write operation to the list of operations that will be
 * requested by the next invocation of execute().   The format of a fifo write
 * request is "wf 0xamod 0xaddress count {0xdata1 0xdata2 ...}".  Write widths are always
 * 32 bits.
 * 
 * @param addr - address to write to.
 * @param amod - address modifier to use.
 * @param data - vector of data to write.
 * @param width - width of the write (D16 or D32).
 */
void
CVMEClient::addFifoWrite(
    uint32_t addr, uint8_t amod,
    const std::vector<uint32_t>& data, DataWidth width) {
    std::stringstream encoder;
    encoder << std::hex << "wf 0x" << unsigned(amod) << " 0x" << addr << " "
        << (width == DataWidth::D16 ? "16" : "32")
        << " {" << std::dec;
    for (auto item : data) {
        encoder << " " << item;
    }
    encoder << " }";
    std::string encoded(encoder.str());
    m_operations.push_back(encoded);
    m_readIndices.push_back(-1);   // Reserve space for the read index.
}   

/**
 * execute
 *    Request the server run the list of operaions and return the
 * read data.  The user can make sense of the read data in any of three ways:alignas
 * *  Keep track of which index in the retuned vector has which read.
 * *  Save the read indices assocated with reads as they are added and
 *    use them.
 * *  Ask the class to tell them, given an index, which read index corresponds
 * to the data.
 * Example of keeping track:
 * 
 * ```c++
 *    client.addWrite(....);
 *    client.addRead(...);                // index 0.
 *    client.addWrite(...);
 *    client.addRead(...);                // index 1.
 * 
 *     auto data = client.execute();
 *     auto firstReadDdata = data[0];
 *     auto secondReadData = data[1];
 * ...
 * ```
 * 
 * Example of saving the returned indices.
 * ```c++
 *    int index = client.addRead(....);
 * ...
 *    auto data = client.execute();
 *    auto mydata = data[index];
 * ```
 * 
 * Example of  asking.
 *    client.addWrite(....);
 *    client.addRead(...);
 *    client.addWrite(...);
 *    client.addRead(...); 
 * 
 *     auto data = client.execute();
 *     auto firstReadIndex = client.readIndex(1);
 *     auto myDatta = data[firstReadIndex];
 * ```
 */
std::vector<std::vector<uint32_t>>
CVMEClient::execute() {
    std::string request = buildRequest();
    std::string reply = transact(request);

    // Is it a success?

    if (reply.substr(0, 5) != "ERROR") {
        return distributeData(reply);
    } else {
        throw std::runtime_error(reply);
    }
}
/**
 * readIndex
 *    Return the read index for an operation in the list.
 * @param operationIndex - index of the read operation in the list.
 * @return int - the index in the returned data at which the data associated
 *   with this read can be found.
 * 
 * @throw std::out_of_range - operationIndex is too big.
 * @throw std::logic_error  - operationIndex references a write.
 */
int
CVMEClient::readIndex(size_t operationIndex) {
    int result = m_readIndices.at(operationIndex);
    if (result >= 0) return result;

    throw std::logic_error("CVMEClient::readIndex - index is for a write");
}

/**
 * reset
 *    clear the list allowing a new one to be built up./
 */
void
CVMEClient::reset() {
    m_operations.clear();
    m_readIndices.clear();
    m_nextReadIndex = 0;
}

///////////////////////// Private utilities

/**
 * buildRequest
 *    Build the request string.
 * 
 * @return std::string - the string transact should send to the server.
 */
std::string
CVMEClient::buildRequest() {
    // First build the operations (tcl formatted) list:

    CTCLInterpreter interp;
    CTCLObject theList;
    theList.Bind(interp);

    for (auto operation : m_operations) {
        theList += operation;
    }
    // now the full Set command:

    std::string request("Set ");
    request += m_name;
    request += " list {";
    request += std::string(theList);
    request += "}";

    return request;
}
/**
 * transact
 *    Write a request string to the server and get the reply back.
 * This operation forms a connection with the server (if not possible,
 * std::runtime_error is thrown). Sends the request string,
 * Reads the reply and closes the connection.
 * 
 * @param request - the request string.
 * @return std::string - the response.
 * 
 */
std::string
CVMEClient::transact(const std::string& request) {
    CTCLInterpreter interp;         // Channels are associated with interps.
    Tcl_Channel socket = Tcl_OpenTcpClient(
        interp.getInterpreter(),
         m_port, m_host.c_str(), nullptr, 0, 0
    );
    if (!socket) {
        throw std::runtime_error(TclRuntimeMessage());
        
    }
    // TODO:   handle short writes.
    int status = Tcl_WriteChars(socket, request.c_str(), request.size());
    if (status != request.size()) {
        std::string msg = TclRuntimeMessage();
        Tcl_Close(interp.getInterpreter(), socket);
        throw std::runtime_error(msg);
    }
    Tcl_Flush(socket);     // Push the data to the server.
    // Use line buffering so our read completes when the server sends its reply line.:

    Tcl_SetChannelOption(interp, socket, "-buffering", "line");
    CTCLObject objReadData;
    objReadData.Bind(interp);

    status = Tcl_GetsObj(socket, objReadData.getObject());
    if (status == -1) {
        std::string msg = TclRuntimeMessage();
        Tcl_Close(interp.getInterpreter(), socket);
        throw std::runtime_error(msg);
    }
    Tcl_Close(interp.getInterpreter(), socket);
    std::string result = std::string(objReadData);

    
    return result;
}
/**
 * distributeData
 *    Take a reply and turn its list of values into a vector of uint32_t
 * 
 * The reply is of the form "OK {data1 ... dataN}".
 * where datan are either single values for single shot reads or 
 * sublists for block/fifo reads.
 * 
 * @param reply - server reply string.
 * @throw CTCLException if the list does not parse properly.
 */
std::vector<std::vector<uint32_t>>
CVMEClient::distributeData(const std::string& reply) {

    std::vector<std::vector<uint32_t>> result;
    // This is a Tcl list of values:

    CTCLInterpreter interp;
    CTCLObject objList;
    objList.Bind(interp);
    objList = reply;

    auto words = objList.getListElements();
    for (int i =1; i < words.size(); i++) { // skip 'OK'.
	    auto word = words[i];
        word.Bind(interp);
        std::vector<uint32_t> per_item;
        if (word.llength() == 1) {
            uint32_t value = int(word);
            per_item.push_back(value);
            
        } else {
            // It's a list of values:
            std::vector<CTCLObject> sublist = word.getListElements();
            for (auto& item : sublist) {
                item.Bind(interp);
                per_item.push_back(int(item));
            }
        }
        result.push_back(per_item);
    }
    return result;
}

/**
 *  get a tcl run time message:
 */
std::string
CVMEClient::TclRuntimeMessage() {

    const char* errnomsg = Tcl_ErrnoMsg(Tcl_GetErrno());
    std::stringstream smsg;
    smsg << "Tcl runtime error on connection/IO " << m_host << ":" << m_port
        << errnomsg;
    std::string  msg(smsg.str());
    return msg;
}
