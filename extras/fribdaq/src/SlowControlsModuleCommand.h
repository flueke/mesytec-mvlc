/**
 * @file SlowControlsModuleCommand.h
 * @brief Header for the slow controls "Module" command.
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
#ifndef MVLC_SLOWCONTROLSMODULECOMMAND_H
#define MVLC_SLOWCONTROLSMODULECOMMAND_H
#include <TCLObjectProcessor.h>
#include <map>
#include <string>

class SlowControlsDriver;

namespace mesytec {
    namespace mvlc {
        class MVLC;
    }
}

/**
 * @class SlowControlsCreator
 *    Create a specific type of slow controls device driver.  Pure virtual class.
 */
class SlowControlsCreator {
public:
    /** create (pure virtual)
     *   Create a new instance of a slow controls device driver.
     *   and stock its configuration with configuration parameter keys.
     * @param controller - pointer to the VME controller the 
     *    driver can use to access the VME.
     * @return  
     *     SlowControlsDriver* - pointer to the new driver ready to be configured.
     */
    virtual SlowControlsDriver* create(mesytec::mvlc::MVLC* controller) = 0;
};

/**
 * @class SlowControlFactory
 *    See the note about this in SlowControlsModuleCommand below.
 */
class SlowControlsFactory {
private:
    std::map<std::string, SlowControlsCreator*> m_creators;
    static SlowControlsFactory* m_pInstance;

    // All canonicals are private therefore with only contruction/destruction implemented
private:
    SlowControlsFactory();
    ~SlowControlsFactory();

    SlowControlsFactory(const SlowControlsFactory&);
    SlowControlsFactory& operator=(const SlowControlsFactory&);
    int operator==(const SlowControlsFactory&);
    int operator!=(const SlowControlsFactory&);

public:
    static SlowControlsFactory* getInstance();

    SlowControlsCreator* find(const std::string& typeName);
    void addCreator(std::string typeName, SlowControlsCreator* creator);
    std::vector<std::string> types() const;

};


typedef std::pair<std::string, SlowControlsDriver*> SlowControlsModule;  // type, driver pair.

/**
 *  @class SlowControlsModuleIndex
 *      Index of modules by name.  In this context a module is a typename/driver pair.
 */
class SlowControlsModuleIndex {
private:
    std::map<std::string, SlowControlsModule> m_modules;   // Modules and types indexed by name.
    static SlowControlsModuleIndex* m_pInstance; // singleton.
public:
    static SlowControlsModuleIndex* getInstance();

    void add(
        const std::string& name, const std::string& type, SlowControlsDriver* pDriver
    );                                                   // add a driver to the dict.
    SlowControlsModule* find(const std::string& name);   // Modules and their type.
    SlowControlsDriver* findDriver(const std::string& name); // when we don't care about the types.
    std::vector<std::pair<std::string, std::string>> list() const;  // list of name/type pairs.


};


/**
 * @class  SlowControlsModuleCommand
 *     This class implements the 'Module' command.  The Module command is responsible
 * for creating, configuring  and querying modules.  It is a command ensemble with the
 * following subcommands:
 * 
 *   - create  - creatse a new module of a specific type.
 *   - config  - Configures a module instance
 *   - cget    - queries the configuration of a module.
 *   - list    - lists the modules that are known and their types optionally filtered by a name pattern.
 *   - types   - lists the module types.
 * 
 * A note that we usea creator factory singleton to create actual modules.
 * We also maintaina module dictioary singleton  used as well by e.g. Set
 * to hold the actual modules.  This supports extending the module types
 * external to our implementation, and the abstract sharing needed by the
 * rest of the slow controls subsystem.
 * 
 */
class SlowControlsModuleCommand : public CTCLObjectProcessor {
    
private:
    mesytec::mvlc::MVLC*  m_pController;          // talk to the VME through this.
    
    // Canonicals:
public:
    SlowControlsModuleCommand(CTCLInterpreter& interp, mesytec::mvlc::MVLC* controller);
    virtual ~SlowControlsModuleCommand();

    // Forbidden canonicals:
private:
    SlowControlsModuleCommand(const SlowControlsModuleCommand&);
    SlowControlsModuleCommand& operator=(const SlowControlsModuleCommand&);
    int operator==(const SlowControlsModuleCommand&);
    int operator!=(const SlowControlsModuleCommand&);

    // Virtual overrides:
public:
    virtual int  operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);

    // Utilities including subcommand processors:
private:
    void create(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void configure(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void cget(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void list(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
    void types(CTCLInterpreter& interp, std::vector<CTCLObject>& objv);
};
#endif