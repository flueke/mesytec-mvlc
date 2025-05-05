/**
 * @file ReadoutCommand.cpp
 * @brief Implementation of  class for all Tcl command recongized by fribdaq-readout
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
#include "ReadoutCommand.h"

using mesytec::mvlc::MVLCReadout;

/**
 *  constructor
 *     @param interp  - references the interpreter the command rusn on.
 *     @param command - command that triggers execution of this class's operator().
 *     @param pState  - pointer to the extended run state.
 *     @param pReadout - Pointer to the readout object.
 * 
 * We just initialize the base class and save the data our subclasses need.
 */
ReadoutCommand::ReadoutCommand(
    CTCLInterpreter& interp, const char* command, 
    FRIBDAQRunState* pState, MVLCReadout* pReadout
) :
    CTCLObjectProcessor(interp, command, TCLPLUS::kfTRUE),
    m_pRunState(pState), m_pReadout(pReadout)
{}

/**
 * destructor
 */
ReadoutCommand::~ReadoutCommand() {}