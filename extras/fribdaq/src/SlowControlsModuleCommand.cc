/**
 * @file SlowControlsModuleCommand.cc
 * @brief Implement the slow controls "Module" command and its helpers.
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
#include "SlowCOntrolsModuleCommand.h"
#include "SLowCOntrolsDriver.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <Exception>

#include <stdexcept>
#include <sstream>

using mesytec::mvlc::MVLC;                 // Just to reduce typos.

////////////////////////// Implement the slow controls factory singleton ///////////////////////


SlowControlsFactory* SlowControlsFactory::m_pInstance(0);

/**
 *  constructor is simple and only accessible to getInstance so _you_ can't call it.
 */
SlowControlsFactory::SlowControlsFactory() {
    if (m_pInstance) {
        throw std::logic_error("BUG: Tried to double create the Slow controls factory!!");
    }
    m_pInstance = this;
}


/**
 * getInstance (static)
 *    Get the singleton instance, creating if needed.
 */
SlowControlsFactory*
SlowControlsFactory::getInstance() {
    if (!m_pInstance) {
        new SlowControlsFactory;              // Sets m_pInstance
    }
    return m_pInstance;
}
/**
 *  find
 *     Find the instance creator for a type:alignas
 * @param typeName -type of device.
 * @return SlowControlsCreator* - pointer to the creator for that type
 * @retval nullptr if there's no type match.
 */
SlowControlsCreator*
SlowControlsFactory::find(const std::string& typeName) {
    auto p = m_creators.find(typeName);
    if (p != m_creators.end()) {
        return p->second;
    } else {
        return nullptr;
    }
}
/**
 * addCreator 
 *     Add a new type creator.  It is a logic error to add a creator for an existing type.
 * 
 * @param typeName - name of the type.
 * @param creator  - Pointer to the creator.
 */
void
SlowControlsFactory::addCreator(std::string typeName, SlowControlsCreator* creator) {
    if (find(typeName)) {
        std::stringstream smsg;
        smsg << "Attempted to create a SlowControlsCreator for " << typename
            << " but there already is one";
        auto msg(smsg.str());
        throw std::logic_error(msg);
    }
}
/**
 * types
 *    @return std::vector<std::string> - the names of types that have registered creators.
 */
std::vector<std::string>
SlowControlsFactory::types() const {
    std::vector<std::string> result;
    for(const auto& p : m_creators) {
        result.push_back(p.second);
    }
    return result;
}

//////////////////////////////////////////// ModuleIndex - the created modules singleton.

SlowControlsModuleIndex* SlowControlsModuleIndex::m_pInstance(nullptr);   // Singleton instance pointer.

/**
 * getInstance
 *     @return SlowControlsModuleIndex* - pointer to the index singleton.
 */
SlowControlsModuleIndex* 
SlowControlsModuleIndex::getInstance() {
    if(!m_pInstance) {
        m_pInstance = new SlowControlsModuleIndex;
    }
    return m_pInstance;
}

/**
 *  find 
 *    Find a module instance by name.
 * 
 * @param name - module name.
 * @return SlowControlsModule*   - pointer to the type/driver pair.
 * @retval nullptr  no module with that name.
 */
SlowControlsModule*
SlowControlsModuleIndex::find(const std::string& name) {
    auto p = m_modules.find(name);
    if (p != m_modules.end()) {
        return &(p->second);              // The actual module pointer
    } else {
        return nullptr;
    }
}
/**
 *  findDriver
 *     Like find but only returns the driver pointer.
 */
SlowControlsDriver*
SlowControlsModuleIndex::findDriver(const std::string& name) {
    auto p = find(name);
    if (!p) {
        return p;
    } else {
        return p->second;
    }
}
/**
 *  list
 * @return std::vector<std::string, std::string>> vector of name/type pairs.
 */

std::vector<std::string, std::string>>
SlowControlsModuleIndex::list() const {
    std::vector<std::pair<std::string, std::string>> result;

    for(const auto& p : m_modules) {
        result.push_back(std::pair(p.first, p.second.first));
    }

    return result;
}

///////////////////////////////////////// Implementation of SlowControlsModuleCommand

/**
 * constructor
 *    @param interp - interpreter on which the command ("Module") is being registered.
 *    @param controller - the VME controller that can be used to talk with the VME crate.
 */
SlowControlsModuleComand::SlowControlsModuleComand(CTCLInterpreter& interp, MVLC* controller) :
    CTCLObjectProcessor(interp, "Module", TCLPLUS::kfTRUE), m_pController(controller)
{}

/**
 * destructor 
 *    well really we never get destroyed but what the heck.
 */
SlowControlsModuleComand::~SlowControlsModuleComand() {}

/**
 * operator()
 *    - Check we have a subcommand.
 *    - Dispatch to the appropraite command handler.
 *    - Turn exceptions -> TCL_ERROR with the exception in result.
 *    - Error if there's an invalid subcommand.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - Encapsulated command words.
 * @return int   - TCL_OK On success, TCL_ERROR if not
 */
int
SlowControlsModuleComand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, obvj);
    try {
        requireAtLeast(objv, 2, "Module: Missing subcommand word");
        std::string subcommand = objv[1];

        if (subcommand == "create") {
            create(interp, objv);
        } else if (subcommand == "config") {
            config(interp, objv);
        } else if (subcommand == "cget") {
            config(interp, objv);
        } else if (subcommand == "list") {
            list(interp, objv);
        } else if (subcommand == "types") {
            types(interp, objv);
        } else {
            std::stringstream smsg;
            smsg << "Module invalid subcommand: " << subcommand;
            std::string msg(smsg.str());
            throw std::logic_error(msg);
        }
    }
    catch (CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    }
    catch (std::exception& e) {
        interp.setResult(e.what());
        return TCL_ERROR;
    }
    catch (std::string msg) {
        interp.setResult(msg);
        return TCL_ERROR;
    }
    return TCL_OK;                  // subcommand handler worked ok.
}