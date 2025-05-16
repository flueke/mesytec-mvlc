/**
 * @file TclServer.h
 * @brief Defines the Entries for the TCL server for slow controls.
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
#ifndef MVLC_TCLSERVER_h
#define MVLC_TCLSERVER_h
#include <string>
#include <stdint.h>
#include <tcl.h>

class CTCLInterpreter;
namespace mesytec {
    namespace mvlc {
        class MVLC;
    }
}


/**
 *  @class ControlServer
 *      This class is intended to run a Tcl server with extended for doing slow control.  As such
 * it needs several things:alignas
 *   -  An MVLC controller object through which it will do VME operations.
 *   -  A port on which to listen for connections.
 *   -  A configuration script that defines the modules that are going to be controlled.alignas
 *   -  A parent interpreter - the Tcl server runs a safe, interpreter that is a slave of
 *      the parent interpreter.
 * Normally the ```start``` public static method is used to start the server.
 * 
 */
class ControlServer {
    // Private data structures:
private:
struct InputClientData {               // ClientData for socket input handler.
    Tcl_Interp*    m_pInterp;          // Raw interpreter.
    Tcl_Channel    m_channel;          // Channel connected to the client.
    std::string    m_command;          // command string being built up.
};

    // Instance data
private:
    CTCLInterpreter&      m_Interp;       // Slave, safe interp.
    mesytec::mvlc::MVLC& m_Controller;   // connection to VME.
    Tcl_Channel          m_Listener;     // channel our server listens on.
    uint16_t             m_servicePort;  // Port we listen on.
    

    static ControlServer* m_pInstance;   // We are a singleton.

    // Construction is private -- must be done by our start method
private:
    ControlServer(
        CTCLInterpreter& slave, mesytec::mvlc::MVLC& controller, 
        const char* configScript, uint16_t port
    );
    ~ControlServer();

    // These canonicals won't be implemented:

    ControlServer(const ControlServer&);
    ControlServer& operator=(const ControlServer&);
    int operator==(const ControlServer&) const;
    int operator!=(const ControlServer&) const;


public:
    // Static methods:

    static void start(
        CTCLInterpreter& parent, mesytec::mvlc::MVLC& controller, 
        const char* configScript, uint16_t port
    );
    ControlServer* getInstance();                  // not a creational -- just in case wee need an instance ptr.
    static void stop();                            // Exit -> slave server to stop it.
    

    // Trampoline to OnConnection.
    static void ConnectionHandler(ClientData pData, Tcl_Channel client, char* host, int port);
    // Trampoline to OnInput
    static void InputHandler(ClientData pData, int mask);

    // Private utilities:

private:
    void OnConnection(Tcl_Channel client, const char* host, int clientPort);
    void OnInput(InputClientData& info);

    void addCommands();                                // Create the interpreter commands.
    void setupServer();                                // Set up the TclServer listener and connection handlers.
    void shutdownServer();                              // Turn off the listener.
    static void shutdownClient(InputClientData& info);        // Shutdown a client connection.  
    void processClientRequest(InputClientData& info);  

};
#endif
