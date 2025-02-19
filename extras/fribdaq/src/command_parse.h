/**
 * @file command_parse.h
 * @brief interface for stupid command parser.
 * @author Ron Fox <fox @ frib dot msu dot edu>
 * 
 *
 *   This software is Copyright by the Board of Trustees of Michigan
 *   State University (c) Copyright 2005.
 *
 *   You may use this software under the terms of the GNU public license
 *   (GPL).  The terms of this license are described at:
 *
 *    http://www.gnu.org/licenses/gpl.txt
 * 
 * @note - this file is private to fribdaq's readout and won't be installed.
 */
#ifndef COMMAND_PARSE_H
#define COMMAND_PARSE_H
#include <string>


/* @todo - hide this in a namespace?  Maye not because its private?*/
/** Parsed command values. from stdin.
 * 
 */
typedef enum _StdinCommands  {
    BEGIN, END, PAUSE, RESUME, TITLE, SETRUN, INVALID
} StdinCommands;

/**
 * The parser produces a ParsedCommand struct.
 * -  s_command is the command keyword.
 * -  For TITLE, s_stringarg is the actual title string.
 * -  For SETRUN s_intarg is the run number provided.
 * -  s_error -for INVALID is the textual parse error.
 */
struct ParsedCommand {
    StdinCommands s_command;
    std::string   s_stringarg;
    int           s_intarg;
    std::string   s_error;
ParsedCommand()  :
    s_command(INVALID) {}

};

/**
 *  @param[in] s - string to test.
 *  @return bool - true if s only consists of whitespace.
 */
bool
isBlank(const std::string& s);
/**
 * parseCommand:
 *  Take a command line and parse it into a ParsedCommand
 *   @param[in] line - input line string.
 *   @return ParsedCommand
 *   @retval if .s_command is INVALID, the parse failed for any
 * of a number of reasons that are in .s_error
 */
ParsedCommand
parseCommand(const std::string& line);

#endif