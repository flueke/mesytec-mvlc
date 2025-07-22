/**
 * @file TclVMEClient.cc
 * @brief Provide a Tcl wrapping of the CVMEClient class.
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
#include <CVMEClient.h>
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <TCLObjectProcessor.h>
#include <tcl.h>
#include <Exception.h>

#include <string>
#include <map>
#include <stdexcept>
#include <sstream>
#include <stdint.h>

class CVMEClientCommand;



 /**
  * This file provides two Tcl command classes:
  * 
  * -  CVMECommand instantiates a CVMEClient object wrapping it in as
  * -  CVMEClientCommand which is a command ensemble in which each
  *    subcommand invokes a method in the CVMEClient that it wraps.
  * -  Package initialization for the mvlcvme package which provides those commands to the 
  * interpreter.
  * 
  *  Here's an example of use:
  * 
  * ```tcl
  *   set client [vme create localhost vme 27000]; # create a client ensemble
  * 
  *   $client addRead 0x10000 0x09 16;   # add a 16 bit read from 0x10000
  *   $client addWrite 0x120000 0x09 0x1234 32;  # Add a 32 bit write of 0x1234 -> 0x120000
  * 
  *   set result [$client execute];               # run the list.
  *   set readValue [lindex $result [$client readIndex 0]];  # Get data from the read
  *   $client reset;                              # Prepare for another list.
  * ...
  *   vme destroy $client;                        # Destroy the client.
  * 
  * ```
  * 
  * @note this file is self-contained.  It's not meant to be linked into any C++ program so there's
  * no associated header.
  */

  /**
   * @class CVMECommand
   *    The command ensemble to create/delete client objects subcommands are:
   * 
   * - create - make a new client object/command. returns the created command name.
   * - destroy - destroys an existing client/object command.
   */
  class CVMECommand : public CTCLObjectProcessor {
private:
    std::map<std::string, CVMEClientCommand*> m_instances;   // Existing instance commands.
    unsigned                             m_serial;      // instance command uniquifier.

    // Supported canonicals:

public:
    CVMECommand(CTCLInterpreter& interp);
    virtual ~CVMECommand();

    // forbidden canonicals.
private:
    CVMECommand(const CVMECommand&);
    CVMECommand& operator=(const CVMECommand&);
    int operator==(const CVMECommand&) const;
    int operator!=(const CVMECommand&) const;

    // overrides:

public:
    virtual int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // Utlities:
private:
    void create(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void destroy(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    std::string nextCommand();
    
};


/** @class CVMEClientCommand
 * 
 *   Wrapper for an instance of CVMEClient.  This is an ensemble with the subcommands:
 * 
 * * addRead - add a read operation to the list.
 * * addWrite - add a write operation to the list.
 * * execute  - Run the list in the server.
 * * readIndex - Get the lindex for the data read from the specified operation index.
 * * reset    - Reset the list preparing it to be restocked and run.
 * 
 * 
 */
class CVMEClientCommand : public CTCLObjectProcessor {
    
    // Internal data:
private:
    CVMEClient*   m_client;    // Wrapped client.

    // Canonicals:
public:
    CVMEClientCommand(CTCLInterpreter& interp, const char* cmd, CVMEClient* client);
    virtual ~CVMEClientCommand();

    // Forbidden canonicals:
private:
    CVMEClientCommand(const CVMEClientCommand&);
    CVMEClientCommand& operator=(const CVMEClientCommand&);
    int operator==(const CVMEClientCommand&) const;
    int operator!= (const CVMEClientCommand&) const;

    // overrides:

public:
    int operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // Utilities:
private:
    void addRead(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void addWrite(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void execute(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void readIndex(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void reset(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    CVMEClient::DataWidth decodeWidth(CTCLObject& width);
    uint8_t validateAmod(CTCLObject& amod);
};



///////////////////////// Implement the CVME  command.

/**
 * constructor
 *    @param interp - interpreter on which to register the command.
 *
 * We register as "vme"
 */
CVMECommand::CVMECommand(CTCLInterpreter& interp) :
    CTCLObjectProcessor(interp, "vme", TCLPLUS::kfTRUE),
    m_serial(0)
{}

/**
 *  destructor - need to destroy all of the clients we have now:
 */
CVMECommand::~CVMECommand() {
    for (auto& p : m_instances) {
        delete p.second;
    }
    m_instances.clear();
}
/** 
 * opertor()
 *    Execute the command.  In our case, we just get the subcommand and dispatch
 * to the corect handler method.  See the description of each of those for the
 * command syntaxes.
 * 
 * @param interp - interpreter runing the command.
 * @param objv   - Encapsulated command words.
 * @return int   - TCL_OK on success, TCL_ERROR if not. In case of error, the reason is
 * left in the result.  If OK, the result depends on the subcommand.
 */
int
CVMECommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);

    try {
        requireAtLeast(objv, 2, "Missing subcommand");
        std::string subcommand = objv[1];

        if (subcommand == "create") {
            create(interp, objv);
        } else if (subcommand == "destroy") {
            destroy(interp, objv);
        } else {
            std::stringstream smsg;
            smsg << subcommand 
                << " is not a valid subcommand, must be 'create' or 'destroy'";
            std::string msg(smsg.str());
            throw std::invalid_argument(msg);
        }
    }
    catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    }
    catch (std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    }
    catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    }

    return TCL_OK;
}

/**
 * create
 *    Create a new instance.
 *     - Ensure the user provided, in order, host, name of Module and connection port.
 *     - Decode the above parameters.
 *     - Create a new client object.
 *     - Construct a new command string and create a client command with that name.
 *     - Enter the command object in the dict.
 * 
 * @param interp - interprter running the command.
 * @param objv   - Encapsulsted command words.
 * 
 * @note all problems are reported as exceptions to the aller.
 */
void
CVMECommand::create(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 5, "vme create : incorrect number of command arguments");

    // Extract/convert the arguments.  Could throw CTCLException.

    std::string host = objv[2];
    std::string moduleName = objv[3];
    int  port = objv[4];

    auto client = new CVMEClient(host.c_str(), moduleName.c_str(), port);
    auto commandName = nextCommand();
    auto clientCommand = new CVMEClientCommand(interp, commandName.c_str(), client);
    m_instances[commandName] = clientCommand;

    interp.setResult(commandName);

}
/**
 *  destroy
 *     Destroy an exsiting client command.
 *  - ensure there's a client command.
 *  - Find the client in the map (error if does not exist).
 *  - Remove the client from the map and delete it.
 * 
 * @param interp - interprter running the command.
 * @param objv   - Encapsulsted command words.
 * 
 */

void
CVMECommand::destroy(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 3, "vme destroy  - missing command to destroy");
    std::string commandName = objv[2];

    auto p = m_instances.find(commandName);
    if (p != m_instances.end()) {
        delete p->second;
        m_instances.erase(p);
    } else {
        std::stringstream smsg;
        smsg << "vme destroy - no such VME client instance command: " << commandName;
        std::string msg(smsg.str());
        throw std::invalid_argument(msg);
    }
}

/**
 *  nextCommand
 *     Generate the next comand to use for a vme instance command.  This will be of the form
 *    vme_n where n is a number that is unique.
 * 
 * @return std::string - the new command.
 */
std::string
CVMECommand::nextCommand() {
    std::string result = "vme_";
    result += std::to_string(m_serial++);
    return result;
}

//////////////////////////////////// Implement CVMEClientCOmmand.

/**
 * constructor:
 *    @param interp - interpreter to register with.
 *    @param cmd    - name of the command we should use.
 *    @param client - A CVMEClient object we are wrapping.
 * 
 * @note the client object is assumed to have been dynamically allocated.  We take ownership
 * and, on destruction delete it.
 */
CVMEClientCommand::CVMEClientCommand(CTCLInterpreter& interp, const char* cmd, CVMEClient* client) :
    CTCLObjectProcessor(interp, cmd, TCLPLUS::kfTRUE),
    m_client(client)
{

}
/**
 *  destructor
 */
CVMEClientCommand::~CVMEClientCommand() {
    delete m_client;
}
/**
 *  operator()
 *     Called when the command is executing.  We require a subcommand keyword and 
 * dispatch to the appropriate subcommand handler.  All subcommand handlers report errors
 * via exceptions which we catch, turn in to error message results and a TCL_ERROR return.
 * 
 * @param interp  - interpreter running the command.
 * @param objv    - Enacapsulated command words.
 * @return int    - Ideally TCL_OK (the command worked), TCL_ERROR if not - see above.
 */
int
CVMEClientCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);

    try {
        requireAtLeast(objv, 2, "VME instance command, missing subcommand");
        std::string subcommand = objv[1];
        if (subcommand == "addRead")  {
            addRead(interp, objv);
        } else if (subcommand == "addWrite") {
            addWrite(interp, objv);
        } else if (subcommand == "execute") {
            execute(interp, objv);
        } else if (subcommand == "readIndex") {
            readIndex(interp, objv);
        } else if (subcommand == "reset") {
            reset(interp, objv);
        } else {
            std::stringstream smsg;
            smsg << std::string(objv[0]) << " - " << subcommand 
                << " Is not a valid subcommand keyword.\n"
                << "Must be one of 'addRead', 'addWrite', 'execute', 'readIndex' or 'reset'" 
                << std::endl;
            std::string msg(smsg.str());
            throw std::invalid_argument(msg);
        }
    }
    catch(std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    }
    catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    }
    catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    }

    return TCL_OK;
}
/*                  subcommand handlers for CVMEClientCommand       */

/**
 * addRead
 *    Add a read operation to the list of operations.
 * This command is of the form:
 * 
 * ```tcl
 * set index [$instance addRead address amod width]
 * ```
 * Where:
 *    - address is the VME address to read from.
 *    - amod - is the address modifier.
 *    - width is one of "16" or "32" indicating the
 *    - index, the result is the index into the list of data returned by
 *      execute where the read data will be found.
 * data transfer width.
 * 
 * So we:
 *    - Ensure there are 5 command words.
 *    - Decode the parameters
 *    - invoke the addRead method of the m_client object.
 * 
 * 
 * @param interp - interpreter running the command.
 * @param objv   - the command words.
 * 
 * @throw std::string - number of command arguments incorrect.
 * @throw CTCLException - integer conversions fail
 * @throw std::invalid_argument - Tha addreess modifier or width are not valid.
 * 
 * @note on success, the result is set to the index of the result data which will contain 
 * the value read by this operation.
 */
void
CVMEClientCommand::addRead(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 5, "VMEInstance addRead - incorrect number of command words");

    uint32_t addr = int(objv[2]);
    uint8_t amod = validateAmod(objv[3]);
    CVMEClient::DataWidth width  = decodeWidth(objv[4]);

    int index = m_client->addRead(addr, amod, width);

    interp.setResult(std::to_string(index));

}
/**
 * addWrite
 *    Add a write operation to he list of operations.   
 *  This command is of the form:
 * 
 * ```tcl
 *  $instance addWrite address amod data width
 * ```
 *  Where:
 *    - address is the VME address to read from.
 *    - amod - is the address modifier.
 *    - data is the value to write.
 *    - width is one of "16" or "32" indicating the
 * 
 * So we:
 *    - Ensure there are 6 command words.
 *    - Decode the address, amod, data and width.
 *    - Invoke the addWrite method of m_client
 * 
 * @param interp  - interpreter running this command.
 * @param objv    - Command words.
 * 
 * @throw std::string - number of command arguments incorrect.
 * @throw CTCLException - integer conversions fail
 * @throw std::invalid_argument - Tha addreess modifier or width are not valid.
 *
 * @note the result is empty on a successful return.
 */
void
CVMEClientCommand::addWrite(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 6, "VMEInstance addWrite - incorrect number of command words" );

    uint32_t addr = int(objv[2]);
    uint8_t amod = validateAmod(objv[3]);
    uint32_t data = int(objv[4]);
    CVMEClient::DataWidth width = decodeWidth(objv[5]);

    m_client->addWrite(addr, amod, data, width);
}
/**
 *  execute
 *     Exacute the existing list of VME operations.  Note that this does not
 * clear the operation list, that requires the reset operation.  This allows a
 * list of operations to be built up and run over and over again if that's needed;
 * without the overhead of recreating the list.alignas
 * 
 * The command has this format:
 * ```tcl
 *    set data [$instance execute]
 * ```
 * 
 * Where data is a list with each element the data from a read.  See addRead and readIndex
 * to know how to find a specific value in this read list.
 * 
 * @param interp  - interpreter running this command.
 * @param objv    - Command words.
 * 
 * @throw std::string - number of command arguments incorrect.
 * @throw std::runtime_error - for failures to execute the list.
 *
 * @note on successful completion the vector of values returned from the client's execute are
 * marshalled into a valid Tcl list.
 */
void
CVMEClientCommand::execute(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 2, "VMEInstance execute - no additional command parameters are expected");

    auto data = m_client->execute();

    // Marshall the result list and set it as the command result:

    CTCLObject result;
    result.Bind(interp);

    for (auto datum : data) {
        result += int(datum);                // no uint_32 +=.
    }

    interp.setResult(result);
    
}
/**
 * readIndex
 *    For the existing list in the client object, determine where in the data returned from execute
 * to find read data.  Form of the command:
 * 
 * ```tcl
 *  set  index [$instance readIndex operationIndex]
 * ```
 * Where:
 *   - operationIndex is the index of a read in the current operation list.
 *   - index  is the index of the list returned from execute at which to find the data read by
 *     the read specified by operationIndex.
 * 
 * Sample usage:
 * ```tcl
 * ...                             # 12 operations added to the list
 *     $instance addRead ....;#   This is operation index 12 e.g.
 * ...
 *     set data [$instance execute]
 *     set myData [lindex $data [$instance readIndex 12]]
 * ``` 
 * 
 * @param interp  - interpreter running this command.
 * @param objv    - Command words.
 * 
 * @throw std::string - number of command arguments incorrect.
 * @throw CTCLException - integer conversions fail
 * @throw std::invalid_argument - The operation number is not a read.
 * @throw std::out_of_range - the operation number is not valid (e.g. too large).
 * 
 * @note on success, the result will be set with an integer value retuned from readIndex.
 */
void
CVMEClientCommand::readIndex(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 3, "VMEInstance readIndex - too many command words");
    int opIndex = objv[2];

    int rdIndex = m_client->readIndex(opIndex);

    interp.setResult(std::to_string(rdIndex));
}
/**
 *  reset
 *    Asks the client to clear the operation list.  Note that after this is done, readIndex will no longer
 * return valid values.
 * 
 * @param interp  - interpreter running this command.
 * @param objv    - Command words.
 * 
 * @throw std::string - number of command arguments incorrect.
 * 
 * @note No result is set on success.
 */
void
CVMEClientCommand::reset(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 2, "VMEInstance reset - no addition command words are expected");

    m_client->reset();
}

///    Utilities for CVMEClientCommand

/**
 * decodeWidth
 *    Decode a width value to a data width enum.
 * 
 * @param width - reference to an object containing a width string.
 * @return CVMEClient::DataWidth - the value from the data width enum for that width value.
 * 
 * @note we evaludate width in string representation so CTCLExceptions will not be thrown but:
 * @throw std::invalid_argument is thrown if the width string isn't one of "16" or "32"
 */
CVMEClient::DataWidth
CVMEClientCommand::decodeWidth(CTCLObject& width) {
    std::string swid = width;
    if (swid == "16") {
        return CVMEClient::DataWidth::D16;
    } else if (swid == "32") {
        return CVMEClient::DataWidth::D32;
    }
    // Invalid:

    std::stringstream smsg;
    smsg << swid << " Is an invalid data width specification. "
        << "must be either '16' or '32'";
    std::string msg(smsg.str());

    throw std::invalid_argument(msg);
}

/**
 * validateAmod
 *    Ensures an address modifier is an integer and fits in a uint8_t
 * 
 * @param amod - object encapsualted address modifier.
 * @return uint8_t - the decoded modifier.
 * 
 * @throw CTCLException - the amod is not a valid integer.
 * @throw std::invalid_argument - the amod unsigned is > 255.
 * 
 */
uint8_t
CVMEClientCommand::validateAmod(CTCLObject& amod) {
    int result = amod; 

    if ((result & 0xff) != result) {
        std::stringstream smsg;
        smsg << "The address modifier specification " << std::hex << result
            << " does not fit into a uint8_t and is therefore invalid.";
        std::string msg(smsg.str());
        throw std::invalid_argument(msg);
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * The code below makes the library a loadable Tcl module.
 *  It defines us as a package named mvlcvme and defines the command "vme"
 * as a generator for CVMEClient instance wrappers.alignas
 * 
 * The initialization function must have "C" bindings and the name assumes the 
 * shared object library will be called libslowControlsClient.so
 * 
 * Note that we need no precautions to load into a safe interp.
 */

 extern "C" {
    int Slowcontrolsclient_Init(Tcl_Interp* pInterp) {
        Tcl_PkgProvide(pInterp, "mvlcvme", "1.0");

        CTCLInterpreter* pEncapInterp = new CTCLInterpreter(pInterp);  // Must stay in scope.
        new CVMECommand(*pEncapInterp);
        return TCL_OK;
    }

    int Slowcontrolsclient_SafeInit(Tcl_Interp* pInterp){
        return Slowcontrolsclient_Init(pInterp);
    }
 }