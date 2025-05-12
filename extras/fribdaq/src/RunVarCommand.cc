/**
 * @file RunVarCommand.h
 * @brief Implements the TCL class that manages ruvars.
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
 * @note The 'title', 'run' variables are stocked in thte map of variables at start time.
 */
#include "RunVarCommand.h"
#include <TCLInterpreter.h>
#include "parser_callbacks.h"
#include <TCLObject.h>
#include <TCLVariable.h>
#include <Exception.h>
#include <sstream>
// Note I don't actually need tha readout object so we won't include it's header.

static const int interval(2000);    // MS between timer pops.

////////////////////////// Implement the dumper class //////////////////////////////////////

/**
 *  constructor:
 *    @param owner - References the outer object (RunVarCOmmand).
 */
RunVarCommand::Dumper::Dumper(RunVarCommand& owner) :
    m_owner(owner) {
        // Set our first timer:

    Set(interval);
}
/**
 * destructor
 *    I belive the base class destructor clears any pending timer.
 * So we can just let it do its stuff:
 */
RunVarCommand::Dumper::~Dumper() {}

/**
 * operator()
 *     Reset the timer and, if the state is Active, dump the variables.
 * Stuff is done in that order so that we can be sure the next timer pop is not
 * influenced by the time it takes to do the dump.
 */
void
RunVarCommand::Dumper::operator()() {
    Set();                                     // reset the timer.
    if (m_owner.m_pRunState->s_runState == Active) dumpVars();

}
/**
 * dumpVars
 *    Do the actual dump:
 * ```
 *    For each variable, 
 *        get its value
 *        construct a setting sctript
 *        Add that setting script to the list of strings.
 *    invoke the parser callbakck's dump_variables.
 * ```
 *   @note if a variable does not exist, It's value will be ```**Not Set**```
 */
void
RunVarCommand::Dumper::dumpVars() {
    CTCLInterpreter* pInterp = m_owner.getInterpreter();    // To fetch variables and build scripts.
    std::vector<std::string> strings;
    for (auto name : m_owner.m_names) {
        CTCLVariable var(name, TCLPLUS::kfFALSE);
        var.Bind(pInterp);
        const char* value = var.Get();
        std::string strValue;
        if (value) {
            strValue = value;
        } else {
            strValue = "**Not Set**";
        }
        std::string script = buildScript(pInterp, name.c_str(), strValue.c_str());
        strings.push_back(script);
    }
    dumpVariables(*m_owner.m_pRunState, strings);
}
/** 
 * buildScript
 *     Builds a script (with appropriate quoting) to restore a varable to a value:
 * 
 *   @param interp - pointer to an interpreter.
 *   @param name   - Variable name.
 *   @param value  - Variable value.
 *   @return std::string - string that contains a set command that will restore the variable.
 */
std::string
RunVarCommand::Dumper::buildScript(CTCLInterpreter* interp, const char* name, const char* value) {
    CTCLObject script;
    script.Bind(interp);
    CTCLObject element;
    element.Bind(interp);

    element = "set";
    script += element;

    element = name;
    script += element;

    element = value;
    script += element;

    return std::string(script);
}


//////////////////////////// Implement the runvar command - maintains the set of variable names //////////////

/**
 * constructor
 *   @param interp - interpreter registering the command.
 *   @param pState - Extended run state context.
 *   @param pReadout - Readout object.
 */
RunVarCommand::RunVarCommand(
    CTCLInterpreter& interp, 
    FRIBDAQRunState* pState, mesytec::mvlc::MVLCReadout* pReadout
) : ReadoutCommand(interp, "runvar", pState, pReadout),
    m_dumper(*this)
{
    // Insert the names that are always there.

    m_names.insert("title");
    m_names.insert("run");
}

/**
 * destructor:
 */
RunVarCommand::~RunVarCommand() {}

/**
 * operator()
 *     Figure out the subcommand and execute it.
 *     Note that to retain compatibility with existing push scripts, no subcommand means
 *     create.
 *
 *  @param interp - interpreter running the command.
 *  @param objv - the command words.
 *  @return int - TCL_OK on success, TCL_ERROR on failure.
 * 
 * @note we use exceptions internally to simplify status checks.
 */
int
RunVarCommand::operator()(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    bindAll(interp, objv);

    try {
        // There must be at least 2 command words:

        requireAtLeast(objv, 2);
        std::string subcommand = objv[1];

        // If there are exactly three, words, this is a create.

        if (objv.size() == 3) {
            create(interp, objv);
        } else if (subcommand == "create") {
            create(interp, objv);
        } else if (subcommand == "delete")  {
            remove(interp, objv);
        } else if (subcommand == "list") {
            list(interp, objv); 
        } else {
            interp.setResult("ruvar - invalid subcommand");
            return TCL_ERROR;
        }
    }
    catch(CException& e) {
        interp.setResult(e.ReasonText());
        return TCL_ERROR;
    }
    catch(std::string& s) {
        interp.setResult(s);
        return TCL_ERROR;
    }
    catch(...) {
        interp.setResult("runvar - unanticipated exception type");
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * create
 *     Create a new variable. 
 * 
 * If there's only a single param we take it as the name.
 *  Note that if the variable does not exist it is created with an empty string value.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - command line words.
 * 
 * @note all errors are reported by exception.
 * @note the caller has ensured there are two values.
 */
void
RunVarCommand::create(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {

    // Teas out the name of the variable.

    std::string name = objv[1];
    if (objv.size() > 2) {
        requireExactly(objv, 3);
        name = std::string(objv[2]);
    }
    CTCLVariable var(name, TCLPLUS::kfFALSE);
    var.Bind(interp);
    if(!var.Get()) {
        var.Set("");                       // Create if it does not exist.
    }
    m_names.insert(name);                  // Add it to the set.
}
/**
 * remove 
 *    Delete a variable for the monitored set.  The variable name must exist in the set.
 *    This does not change the variable itself.
 * 
 * @param interp - interpreter running the command.
 * @param objv   - command line words.
 * 
 * @note all errors are reported by exception.
 */
void
RunVarCommand::remove(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    // Must be three words:

    requireExactly(objv, 3);
    std::string name = objv[2];
    auto p = m_names.find(name);
    if (p == m_names.end()) {
        std::stringstream msg;
        msg << "runvar remove - " << name << " is not in the list of monitored variables";
        std::string smsg(msg.str());
        throw smsg;
    }
    m_names.erase(p);
}
/**
 * list
 *   Takes an optional pattern (defaults to "*").  Sets the result to the (possibly empty) list of
 * matching names.alignas
 * 
 * @param interp - interpreter running the command.
 * @param objv   - command line words.
 * 
 */
void
RunVarCommand::list(CTCLInterpreter& interp, std::vector<CTCLObject>& objv) {
    std::string pattern("*");
    requireAtMost(objv, 3);
    if (objv.size() == 3) {
        pattern = std::string(objv[2]);
    }
    CTCLObject result;
    result.Bind(interp);
    for (auto name : m_names) {
        if (Tcl_StringMatch(name.c_str(), pattern.c_str())) {
            CTCLObject item; 
            item.Bind(interp);
            item = name;
            result += item;
        }
    }

    interp.setResult(result);
}