/**
 * @file command_parse.cc
 * @brief interface for stupid command parser.
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
 * @note - this file is private to fribdaq's readout 
 * @note - this is  extracted from fribdaq_readout.cc to allow unit tests to be run
 * against it.
 */
#include "command_parse.h"
#include <sstream>
#include <algorithm>
#include <ctype.h>
#include <map>


// Map between command words and enum
static std::map<std::string, StdinCommands> commandMap = {
    {"BEGIN", BEGIN}, {"begin", BEGIN},
    {"EXIT", EXIT}, {"exit", EXIT},
    {"END", END}, {"end", END},
    {"PAUSE", PAUSE}, {"pause", PAUSE},
    {"RESUME", RESUME}, {"resume", RESUME},
    {"TITLE", TITLE},  {"title", TITLE},  // Remainder of line is a title. 
    {"SETRUN", SETRUN}, {"setrun", SETRUN} // Remainder of line is a + integer run number.
};

/* trim leading/trailing whitespace from a string: */
static std::string
trim(std::string& str) {
    std::string result(str);  
    
    // Trim the front:

    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));

    // Trim from end:

    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), result.end());

    return result;
}
/*  True if the parameter is a string that is only whitespace */
bool
isBlank(const std::string& str) {
    return std::all_of(str.begin(), str.end(), isspace);
}
/*
 *  parseCommand implementation.
 */
ParsedCommand
parseCommand(const std::string& line) {
    ParsedCommand parsed;

    // Get the command word and map it to a command value:

    std::stringstream words(line); 
    std::string commandword;
    std::string remainder;
    words >> commandword;
    if (commandMap.find(commandword) == commandMap.end()) {
        parsed.s_error ="Invalid command keyword";
        // invalid (empty goes here too):
        return parsed;   //   Initializes to invalid
    }
    parsed.s_command = commandMap[commandword];
    std::getline(words, remainder);
    switch (parsed.s_command) {
    case BEGIN:
    case END:                           // These should have at most a whitespace remainder:
    case PAUSE:
    case RESUME:
    case EXIT:
        if (!isBlank(remainder)) {
            parsed.s_command = INVALID;
            parsed.s_error = "Unexpected characters following the command keyword";
        }
        break;
    case TITLE:                        // Remainder is the arg.
        parsed.s_stringarg = remainder;
        if (isBlank(remainder)) {
            parsed.s_command = INVALID;
            parsed.s_error = "TITLE needs a title string tail";
        } else {
            parsed.s_stringarg = trim(remainder);
        }
        break;
    case SETRUN:  {                    // Remainder is a postitive integer.
            
            std::stringstream tail(remainder);
            remainder = "";        // Since getline and end won't fill this.
            tail >> parsed.s_intarg;
            if (tail.bad() || tail.fail()) {
                parsed.s_command = INVALID;
                parsed.s_error = "SETRUN needs a run number";
            } else {
                // Run must be > 0:

                if (parsed.s_intarg <= 0) {
                    parsed.s_command = INVALID;
                    parsed.s_error = "SETRUN's run number must be > 0";
                } else {
                    // The remainder now must be empty:

                    getline(tail, remainder);
                    
                    if (!isBlank(remainder)) {
                        parsed.s_command = INVALID;
                        parsed.s_error = "Unexpected characters following the command keyword";
                    }
                }
            }
        }
        break;
    default:                                      // Error to land here:
        parsed.s_command = INVALID;
        parsed.s_error = "The command parser has an error and took a case it should not have";
    }
    return parsed;
}
