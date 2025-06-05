/**
 * @file TclServer.h
 * @brief Implements the entries for the TCL server for slow controls.
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

#include "TclServer.h"
#include "SlowControlsModuleCommand.h"
#include "SlowControlsSetCommand.h"
#include "SlowControlsGetCommand.h"
#include "SlowControlsUpdateCommand.h"
#include "SlowControlsMonCommand.h"
#include "TclVMEWrapper.h"

#include <TCLInterpreter.h>
#include <TCLVariable.h>
#include <tcl.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <stdexcept>
#include <Exception.h>
#include <sstream>
#include <string.h>
#include <stdlib.h>   
#include <iostream>


using mesytec::mvlc::MVLC;

// Constants:

const unsigned READ_SIZE(256);                              // Size of read buffer.

 /** 
  * static class data:
  */

  ControlServer*  ControlServer::m_pInstance(0);            // filled in by 'start'.

  /**
   * start (static)
   *     start the conntrol server.  This is done by instantiating it and
   * setting m_pInstance to the new instance.  If there already is an instance,
   * we throw an std::logic_error.
   * 
   * @param parent - the parent interpereter
   * @param controller - The MVCL controller instance reference
   * @param configScript - When the intepreter is set up, this is run if not null.
   * @param port     - Server listener port.
   */
  void
  ControlServer::start(CTCLInterpreter& parent, MVLC& controller, const char* configScript, uint16_t port) {

    // Dont' let it start twice:

    if (m_pInstance) {
        throw std::logic_error("Slow controls server is already running");
    }


    // Make the slave.  I wanted to make the slave a safe interpreter _but_
    // I could not figure out how to make tcllib packages loadable so that
    // meant that snit, and itcl - the packages used to write Tcl drivers
    // could not be loaded....sadly this is a security hole...as
    // file -delete is exposed. e.g.

    Tcl_Interp* rawInterp = Tcl_CreateSlave(parent.getInterpreter(), "slow-controls", false);
    if (!rawInterp)  {
        throw std::runtime_error("Failed to create control server slave interpreter");
    }
    CTCLInterpreter* pInterp = new CTCLInterpreter(rawInterp);

    // propagate the auto_path from the master to the slave interpreter so that 
    // control drivers can be written in snit or itcl.

    CTCLVariable auto_path(&parent, "auto_path", TCLPLUS::kfFALSE);
    CTCLVariable slave_auto_path(pInterp, "auto_path", TCLPLUS::kfFALSE);

    slave_auto_path.Set(auto_path.Get());

    new ControlServer(
        *pInterp, controller, configScript, port
    );
    
  }
  /**
   *  getinstance (static)
   *     Return the singleton instance...note that this does not create the object if it does not
   * exist but returns nullptr.  Use ```start``` above to create an instance.
   * 
   * @return ControlServer*
   * @retval nullptr - the control server isn't running.  Maybe never was started or was stopped.
   * 
   */
  ControlServer*
  ControlServer::getInstance() {
    return m_pInstance;
  }
  /**
   *  stop (static)
   *     Destroy the instance of the control server (if it exists)
   * 
   * @note tha destructor will clear channel handlers for the listener and close that channel.
   * This ensures that ConnectionHandler won't be called with a instance.
   * 
   * @note It is possible that active connections will invoke the InputHandler trampoline so that
   * must detect the nulled object, turn off file events for that channel and close the channel.
   */
  void
  ControlServer::stop() {
    if (m_pInstance) {
        delete m_pInstance;
        m_pInstance = nullptr;
    } else {
        throw std::logic_error("Tried to stop the control server when it was not running");
    }
  }

  /**
   * Connection Handler (static)
   *     This is called when the connection channel has a new connection from a client
   * for the server.  Should not happen if the object does not exist, however if it
   * does, we'll ignore this
   * Otherwise, we establish object context and call OnConnection - which does the work
   * in object conetxt.
   * 
   * @param pData - client data - in this case the a dynamic ponter to  Tcl_Channel of the server listener
   * @param client - Channel connected to the client.
   * @param host   - host that formed the connection to us.
   * @param port   - Port on which the connection is formed.
   */
  void
  ControlServer::ConnectionHandler(ClientData pData, Tcl_Channel client, char *host, int port) {
    if (m_pInstance) {
        m_pInstance->OnConnection(client, host, port);
    } else {
        // Delete our dynamic copy of the listener channel.  We're going to hope the
        // channel itself is closed by the instance as it was deleted.  That's evidently
        // the only way to cancel the connection handler.

        Tcl_Channel* listener = reinterpret_cast<Tcl_Channel*>(pData);
        delete listener;
        
        // Well we can't close without an interpreter - let's hope the close is in process.
        // In any event the previous lines keep us from getting called again.
    }
  }

  /**
   * InputHandler (static)
   *     THis is called when a client has input for us.
   *     If the object does not exist any more we shutdown the event handler and close the channel.
   *     which closes the connection.  We don't look at errors from that operation.alignas
   *    
   *     If the instance exists, we transfer control to OnInput in object context.alignas
   * 
   * @param pData  - Actually a pointer to an InputClientData
   * @param mask   - mask of events.  We only set up Tcl_Readable so....
   *
   */
  void
  ControlServer::InputHandler(ClientData pData, int mask) {
    InputClientData* pInfo  = reinterpret_cast<InputClientData*>(pData);

    if (m_pInstance) {
        m_pInstance->OnInput(*pInfo);
    } else {
        // No object to shut us and the channel down; and delete the client data block.

        shutdownClient(*pInfo);
    }
  }

/**
 *  constructor 
 *    THis is private.  The way to get this called is to invoke the ```start``` method.
 * That ensures that only one of us is started at a time and that m_pInstance is set
 * ensuring our singleton-nature.  It also does the setup needed to pass the parameters:
 * 
 * @param slave - Reference to the wrapped slave interpreter that will be executing the commands sent to us.
 *                slave is a safe interpreter ensuring  the clients can't do nasty things to us.
 *                We gain ownershp of the slave and will delete it in the destructor.
 * @param controller - MVLC controller that allows our modules to perform VME operations.
 * @param configScript  - Configuration script that, presumably creates and configures slow control modules.
 *               This is run after the interpreter is setup and commands added to it.
 * @param port   - The TCP Port on which the server should listen for connections.
 *              In general, since fribdaq-readout runs as an ordinary user, this should be
 *              larger than 1024 however we don't require that.  If the server startup fails,
 *              we just throw an std::runtime_error that propagates out of the creation.
   * 
*/
ControlServer::ControlServer(
    CTCLInterpreter& slave, MVLC& controller, const char* configscript, uint16_t port
) : m_Interp(slave), m_Controller(controller),  m_servicePort(port)
{
    // Save out instance pointer:
    // We need to do this early becuse e.g. snit drivers will need to get our
    // intepreter which depends on m_pInstance being defined (e.g. getInstance
    // returning something meaningful).
    
    m_pInstance = this;   
    addCommands();

    // Run the configuratilon script - errors result in exceptions that bubble back:

    try {
        m_Interp.EvalFile(configscript);
    }
    catch (CException& e) {
        // Script execution error is fatal:
        std::cerr << "Sourcing slow controls configuration script: "
            << configscript << " failed: "
            << std::endl << e.ReasonText() << std::endl;
        CTCLVariable errorinfo(&m_Interp, "errorInfo", TCLPLUS::kfFALSE);
        const char* traceback = errorinfo.Get();
        if (traceback) {
            std::cerr << traceback << std::endl;
        }
        Tcl_Exit(EXIT_FAILURE);
    }

    setupServer();                        // Sets m_Listener and enables connection handler.
}
/**
 * destructor:
 *    Shutdown the server and free the slave interpreter, which destroys it.
 */
ControlServer::~ControlServer() {
    shutdownServer();
    delete &m_Interp;
}
/**
 * OnConnection
 *    Object context connection handler. What we must do is:
 * - Allocate and fill in  an InputClientDataBlock.
 * - Set an InputHandler for the client.
 * 
 * We leave the socket in blocking mode, (see OnInput), we do set -buffering to none so that out output
 * does not need to be flushed.
 * 
 * @param client - the channel open on the client through which requests/repsonses are done.
 * @param host   - host that connected to us.
 * @param clientPort - port on which the client connected.
 * 
 * @note we do log the connection with spdlog::info.
 */
void
ControlServer::OnConnection(
    Tcl_Channel client, const char* host, int clientPort
) {
    spdlog::info("Connection to control server from host: {} on port {}", host, clientPort);

    // Fill in the client data for the input handler.

    InputClientData* pClientData = new InputClientData;
    pClientData->m_pInterp = m_Interp.getInterpreter();
    pClientData->m_channel = client;

    // Set the options fon the client:

    Tcl_SetChannelOption(pClientData->m_pInterp, client, "-buffering", "none");
    Tcl_CreateChannelHandler(
        client, TCL_READABLE, 
        InputHandler, reinterpret_cast<ClientData>(pClientData)
    );
}
/**
 *  OnInput
 *     Called in object context when a client is readable.
 *     The channel is se in non-blocking mode and a ReadChanrs is used to get
 *     a chunk  of data  from the input.  IF we are at EOF,
 *     The input handler is de-registered and we close the channel and delete the InputCLientData block.
 * 
 *     After the read the channel is taken out of nonblock mode.
 *     If we have a complete command in the client data block after reading the data,
 *     call processClientRequest to handle it.
 * 
 * @param info - Reference to the InputClientData struct that conains stuff we need.
 */
void
ControlServer::OnInput(InputClientData& info) {
    // Set non-blocking mode for the duration of the read:

    Tcl_SetChannelOption(info.m_pInterp, info.m_channel, "-blocking", "0");

    // The read:

    char buffer[READ_SIZE+1];
    memset(buffer, 0, sizeof(buffer));    // Ensure there will be a null terminator.
    int nRead = Tcl_Read(info.m_channel, buffer, READ_SIZE);

    // Turn blocking back on right away.

    Tcl_SetChannelOption(info.m_pInterp, info.m_channel, "-blocking", "1");

    // Errors - log and shutdown the client:
    // TODO:   Adding the host and port to the client data will make better 
    //        error reporting possible:
    if (nRead < 0) {
        spdlog::warn("Error reading data from slow control client - shutting down the client");
        shutdownClient(info);                           // Info is no longer valid.
    } else if (nRead == 0) {
        if (Tcl_Eof(info.m_channel)) {
            // Client closed the connection.

            spdlog::info("Closing slow control client - disconnected");
            shutdownClient(info);
        } 
        // Somehow readable, not closed but no data...that's ok.
    } else {
        // Actual data, append it to the command.  If we have a full command,
        // process it.

        info.m_command += buffer;
        if (Tcl_CommandComplete(info.m_command.c_str())) {
            processClientRequest(info);                       // Does the reply and clears the command.
        }
    }


}

/**
 * addCommands
 *    Adds all slow controls command extensions to m_Interp.
 */
void
ControlServer::addCommands() {
    new SlowControlsModuleCommand(m_Interp, &m_Controller);
    new SlowControlsSetCommand(m_Interp);
    new SlowControlsGetCommand(m_Interp);
    new SlowControlsUpdateCommand(m_Interp);
    new SlowControlsMonCommand(m_Interp);
    new TclVmeWrapper(m_Interp, &m_Controller);
    new TCLVmeListWrapper(m_Interp, &m_Controller);
}

/**
 * setupServer
 *    Open a TCPserver with our connection handler in s_servicePort.
 * 
 * side effect: We set m_Listener with the channel that was thus created.
 * Errors throw runtime error.
 */
void
ControlServer::setupServer() {
    Tcl_Channel* pListener =new Tcl_Channel; // Really the only way to get the channel to the client data.
    *pListener = Tcl_OpenTcpServer(
        m_Interp.getInterpreter(), m_servicePort, nullptr,
        ConnectionHandler, reinterpret_cast<ClientData>(pListener)
    );
    if (*pListener) {
        m_Listener = *pListener;
    } else {
        //TODO: Get the reason for the failure from errno e.g.
        delete pListener;
        std::stringstream smsg;
        smsg << "Failed to start control server listener on port " << m_servicePort << std::endl;
        smsg << "Be sure the port is > 1024 so that it is unprivileged, or \n";
        smsg << "Better yet use the port manager to allocate it.";

        std::string msg(smsg.str());
        throw std::runtime_error(msg);
    }
}
/**
 * shutdownServer
 *     Close the listner channel.
 */
void
ControlServer::shutdownServer() {
    Tcl_Close(m_Interp.getInterpreter(), m_Listener);
}

/** 
 * shutdownClient (static)
 *    - Disable callbacks on a client channel.
 *    - CLose the channel.
 *    - Delete the info block.
 * @param info - references the client information block that goes with this client.
 */
void
ControlServer::shutdownClient(InputClientData& info) {
    
        auto pInfo = &info; 
        Tcl_DeleteChannelHandler(
            info.m_channel, 
            ControlServer::InputHandler, reinterpret_cast<ClientData>(pInfo)
        );
        Tcl_Close(info.m_pInterp, info.m_channel);

        delete pInfo;
}
/**
 * processClientRequest
 *    Called when the client iformation block is known to have a 
 * complete command. That command is executed in our slave interpreter.
 * On success, we just send back the interpreter's result.
 * On failure, we prepend the result with the string "ERROR - ".
 * Indicating this is an error result.
 * 
 * We don't worry about the client droppping the connection between the
 * receipt of the command and the reply.  What will happen then is our
 * OnInput will be called for that client and we'll detect an end file on the
 * input, shutting the client down.  Yep it's true that the response will
 * go into the Ozone but it's not really clear where else it could go that is
 * meaningful.
 * 
 * @param info - reference to the client information block, which has the
 *    icommand.  Note that the command string will be cleared before we return.
 */
void
ControlServer::processClientRequest(InputClientData& info) {
    bool success(true);
    try {
        m_Interp.GlobalEval(info.m_command);
    } 
    catch (CException e) {
        success = false;
    }
    info.m_command = "";

    // Formulate and send the response:

    std::stringstream sresponse;
    if (!success) {
        sresponse << "ERROR - ";
    }
    sresponse << m_Interp.GetResultString() << std::endl;
    std::string response(sresponse.str());
    Tcl_WriteChars(info.m_channel, response.c_str(), -1);
}
