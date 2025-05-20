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
#include "SlowControlsModuleCommand.h"
#include "SlowControlsDriver.h"
#include <TCLInterpreter.h>
#include <TCLObject.h>
#include <Exception.h>
#include <XXUSBConfigurableObject.h>

#include <stdexcept>
#include <sstream>

using mesytec::mvlc::MVLC;                 // Just to reduce typos.
using XXUSB::CConfigurableObject;

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
        smsg << "Attempted to create a SlowControlsCreator for " << typeName
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
        result.push_back(p.first);
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
 * add
 *    Add a new Module to the dict.
 * 
 * @param name - name of the module.
 * @param typeName - name of the module type.
 * @param pDriver - Pointer to the driver instance.
 * 
 * @note - It is assumed the caller will have ensured there
 *    is no driver with that name.  If there is, it will be
 *    replaced and memory will leak because the driver instance
 *    won't be deleted.`
 */
void
SlowControlsModuleIndex::add(
    const std::string& name, const std::string& typeName, SlowControlsDriver* pDriver
) {
    SlowControlsModule entry = std::make_pair(
        std::string(typeName), pDriver
    );
    m_modules.emplace(std::make_pair(std::string(name), entry));
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
        return nullptr;
    } else {
        return p->second;
    }
}
/**
 *  list
 * @return std::vector<std::string, std::string>> vector of name/type pairs.
 */

std::vector<std::pair<std::string, std::string>>
SlowControlsModuleIndex::list() const {
    std::vector<std::pair<std::string, std::string>> result;

    for(const auto& p : m_modules) {
        result.push_back(std::make_pair(p.first, p.second.first));
    }

    return result;
}

///////////////////////////////////////// Implementation of SlowControlsModuleCommand

/**
 * constructor
 *    @param interp - interpreter on which the command ("Module") is being registered.
 *    @param controller - the VME controller that can be used to talk with the VME crate.
 */
SlowControlsModuleCommand::SlowControlsModuleCommand(CTCLInterpreter& interp, MVLC* controller) :
    CTCLObjectProcessor(interp, "Module", TCLPLUS::kfTRUE), m_pController(controller)
{}

/**
 * destructor 
 *    well really we never get destroyed but what the heck.
 */
SlowControlsModuleCommand::~SlowControlsModuleCommand() {}

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
SlowControlsModuleCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);
    try {
        requireAtLeast(objv, 2, "Module: Missing subcommand word");
        std::string subcommand = objv[1];

        if (subcommand == "create") {
            create(interp, objv);
        } else if (subcommand == "config") {
            configure(interp, objv);
        } else if (subcommand == "cget") {
            cget(interp, objv);
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
////////////////////////////////// Private utilities -- subcommand handlers ////////////////////////

/**
 * create
 *    Create a new modujle
 *    - Counting the Module verb, there must be exactly 4 command words.
 *    - There cannot be a module of that name in the index.
 *    - We must be able to find a creator in the factory.
 *    - If all that works, we ask the creator to creat the module and then we put it
 *      in the index.
 * 
 * On success, we return the module name.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - THe command words -- all have been bound to interp.
 * 
 * @note we signal errors with exceptions that the caller turns into result 
 *       and TCL_ERROR returns.
 */
void
SlowControlsModuleCommand::create(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireExactly(objv, 4, "Module create - incorrect number of command parameters");
    std::string typeName = objv[2];
    std::string name     = objv[3];

    // Ensure there's no such module:

    if (SlowControlsModuleIndex::getInstance()->find(name)) {
        std::stringstream smsg;
        smsg << "A module named " << name << " has already been created";
        std::string msg(smsg.str());

        throw std::logic_error(msg);
    }
    // Get the creator for that type - if it does not exist, that's an error too.

    auto pCreator = SlowControlsFactory::getInstance()->find(typeName);
    if (!pCreator) {
        std::stringstream smsg;
        smsg << "The module type : " << typeName << " does not exist";
        std::string msg(smsg.str());

        throw std::logic_error(msg);
    }
    //  Everything should work now:

    auto driver = pCreator->create(m_pController);    
    SlowControlsModuleIndex::getInstance()->add(name, typeName, driver);
    interp.setResult(name);
}
/**
 * configure
 *    Configure a driver.  This has an indefinite number of command words but:
 *    - At least 5
 *    - The total number of command words must be odd (Module config name cfgparam cfgvalue ...).
 *    
 *    -  There must be a matching module name.
 *    -  For each config name/value pair, attempt to configure the module driver.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - The command words -- all have been bound to interp.
 * 
 * @note that if there are configuration errors, the successfully configured items will leave the module
 * partly configured.
 * 
 * @note all errors are reported as exceptions wich the caller turns into result/TCL_ERROR.
 * 
 * @note if successfully configured, the module's told to reconfigure.   Partially successful 
 * configs don't do this.
 */
void
SlowControlsModuleCommand::configure(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireAtLeast(
        objv, 5, 
        "Module config - not enough command words at least one parameter must be configured"
    );
    if (!objv.size() % 2) {
        throw std::logic_error("Each configuration option must also have a value.");
    }

    std::string name = objv[2];
    auto pDriver = SlowControlsModuleIndex::getInstance()->findDriver(name);
    if (!pDriver) {
        std::stringstream smsg;
        smsg << "Module config : There is no known module named: " << name;
        std::string msg(smsg.str());

        throw std::logic_error(msg);
    }

    CConfigurableObject* pConfig = pDriver->getConfiguration();
    for (int i = 3; i < objv.size(); i+=2) {
        std::string optName = objv[i];
        std::string optval  = objv[i+1];

        pConfig->configure(optName, optval);   // could throw.
    }
    // Tell the driver it was reconfigured:

    pDriver->reconfigure();

    interp.setResult(name);
}

/**
 *  cget 
 *    Either dump the entire configuration of a driver --  or a single value.
 * We therefore recognize the following two command forms:
 * 
 * ```tcl
 *   Module cget name;    # Dumps the config as a list of name/value pairs
 *   Module cget name option;   # Returns the value of 'option'.
 * ```
 * 
 * - There must be at least 3, and no more than four parameters.
 * - Find the module and complain if it does not exist.
 * - If there's an option name, set the result to the value of that option.
 * - If there's no option name, construct a list of option name/value pairs and set the
 *   result to that.
 * @param interp - interpreter running the command.
 * @param objv   - The command words -- all have been bound to interp.
 * 
 * 
 */
void
SlowControlsModuleCommand::cget(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireAtLeast(objv, 3, "Module cget must have at least a module name");
    requireAtMost(objv, 4, "Module cget -- too man command line parameters.");

    std::string name = objv[2];
    auto pDriver = SlowControlsModuleIndex::getInstance()->findDriver(name);
    if(!pDriver) {
        std::stringstream smsg;
        smsg << "Module cget - there is no module named: " << name;
        std::string msg(smsg.str());

        throw std::logic_error(msg);
    }

    CConfigurableObject* pConfig = pDriver->getConfiguration();
    if (objv.size() == 4) {                       // Specific param.
        std::string optname = objv[3];
        interp.setResult(pConfig->cget(optname));
    } else {                                   // All params.
        CTCLObject result;
        result.Bind(interp);
        auto config = pConfig->cget();     // Array of option/value pairs.
        for (auto entry: config) { 
            CTCLObject item;
            item.Bind(interp);

            CTCLObject option;
            option.Bind(interp);             // Option name.
            option = entry.first;            

            CTCLObject value;
            value.Bind(interp);             // option value.
            value = entry.second;

            item += option;          // THe pair to add to the result list.
            item += value;

            result += item;
        }
        interp.setResult(result);
        
    }
}
/**
 * list
 *    Lists the modules that are known and their types.   The result is a list of pairs of name/type.
 * An optional pattern can filter the list to only those modules whose names match the glob pattern.
 * 
 * - Ensure there are at most 3 parameters.
 * - If there is a pattern, it overrides the default pattern of "*"
 * - Get the list from the module index,
 * - FOr each item in the list where the name matches the pattern, add the pair to the result.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - the command words.
 */
void
SlowControlsModuleCommand::list(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireAtMost(objv, 3, "Module list: Too many command line parameters");

    auto listing = SlowControlsModuleIndex::getInstance()->list();

    std::string pattern = "*";
    if (objv.size() == 3) {
        pattern = std::string(objv[2]);
    }

    CTCLObject result;
    result.Bind(interp);
    for (auto module : listing) {
        // Does the name match the pattern?

        auto name = module.first;
        auto typeName = module.second;

        if (Tcl_StringMatch(name.c_str(), pattern.c_str())) {
            CTCLObject pair;
            pair.Bind(interp);

            CTCLObject oName;
            oName.Bind(interp);
            oName = name;

            CTCLObject oType;
            oType.Bind(interp);
            oType = typeName;

            pair += oName;
            pair += oType;

            result += pair;
        }
        
    }

    interp.setResult(result);
}

/**
 * types
 *    Return a list of the supported types.alignas
 * @param interp - interpreter running the command.
 * @param objv   - The command line parameters.
 * 
 * @note:  An optional. pattern can be used to filter the type list by 
 *    those that glob match.
 */
void
SlowControlsModuleCommand::types(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    requireAtMost(objv, 3, "Module types: too many command parameters");

    std::string pattern ("*");
    if (objv.size() == 3) {
        pattern = std::string(objv[2]);
    }

    auto list = SlowControlsFactory::getInstance()->types();

    CTCLObject result;
    result.Bind(interp);
    for (auto typeName : list) {
        if (Tcl_StringMatch(typeName.c_str(), pattern.c_str())) {
            CTCLObject oType;
            oType.Bind(interp);
            oType = typeName;

            result += oType;
        }
    }

    interp.setResult(result);
}

