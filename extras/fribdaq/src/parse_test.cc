/** 
 * @file parse_test.cc
 * @brief test suite for command_parse's parseCommand function
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
 */

 /**
  * @note I don't verify the error messages as that leads to fragile
  * tests that need adjustment as error messages get 'better'.
  */

 #include "command_parse.h"
 #include <gtest/gtest.h>


 TEST(command_parse, EmptyInvalid) {
    // Empty string gives invalid:
    std::string line;
    auto result = parseCommand(line);
    ASSERT_EQ(result.s_command, INVALID);
 }
 TEST(command_parse, BadCommandInvalid) {
    std::string line("Junky");
    auto result = parseCommand(line);
    ASSERT_EQ(result.s_command, INVALID);
 }
 TEST(command_Parse, BEGIN_OK) {
    std::string line1("BEGIN");
    std::string line2("begin");

    auto result1 = parseCommand(line1);
    auto result2 = parseCommand(line2);

    ASSERT_EQ(result1.s_command, BEGIN);
    ASSERT_EQ(result2.s_command, BEGIN);
 }
 TEST(command_parse, BEGIN_WS_TAIL) {
    // whitespace tail is ok:

    std::string line = ("begin     ");
    auto result = parseCommand(line);
    ASSERT_EQ(result.s_command, BEGIN);
 }
 TEST(command_parse, BEGIN_NOWS_TAIL) {
    // Non whitespace tail is invalid

    std::string line("begin run");
    auto result = parseCommand(line);
    ASSERT_EQ(result.s_command, INVALID);
 }
 // The other commands without an argumentn get teste in the 
 // same code so we're not going to be quite as complete in the testing
 // as we were for begin.

 TEST(command_parse,  END) {
    std::string line1("END");
    std::string line2("end");

    auto result1=parseCommand(line1);
    auto result2=parseCommand(line2);

    ASSERT_EQ(result1.s_command, END);
    ASSERT_EQ(result2.s_command, END);
 }

 TEST(command_parse, PAUSE) {
    std::string line1("pause");
    std::string line2("PAUSE");

    auto result1=parseCommand(line1);
    auto result2=parseCommand(line2);

    ASSERT_EQ(result1.s_command, PAUSE);
    ASSERT_EQ(result2.s_command, PAUSE);
 }

 TEST(command_parse, RESUME) {
    std::string line1("resume");
    std::string line2("RESUME");

    auto result1=parseCommand(line1);
    auto result2=parseCommand(line2);

    ASSERT_EQ(result1.s_command, RESUME);
    ASSERT_EQ(result2.s_command, RESUME);
 }

 // Title must have a tail and that will be the s_stringarg

 TEST(command_parse, TITLE_NO_TAIL) {
    std::string line1("TITLE");    // Invalid.
    std::string line2("TITLE     "); // also invalid...title cannot be only whitespace.

    auto result1 = parseCommand(line1);
    auto result2 = parseCommand(line2);

    ASSERT_EQ(result1.s_command, INVALID);
    ASSERT_EQ(result2.s_command, INVALID);
 }

 TEST(command_parse, TITLE_OK) {
    std::string line1("TITLE This is a title string");
    std::string line2("TITLE      This is a title string");  // Leading space is unimportant.

    auto result1= parseCommand(line1);
    auto result2 = parseCommand(line2);

    ASSERT_EQ(result1.s_command, TITLE);
    ASSERT_EQ(result1.s_stringarg, std::string("This is a title string"));

    ASSERT_EQ(result2.s_command, TITLE);
    ASSERT_EQ(result2.s_stringarg, std::string("This is a title string"));
 }

 // SETRUN must have a tail and it must be a single integer > 0.

 TEST(command_parse, SETRUN_NOTAIL) {
    std::string line1("SETRUN");
    std::string line2("SETRUN    ");    // need more than whitespace.

    auto result1 = parseCommand(line1);
    auto result2 = parseCommand(line2);

    ASSERT_EQ(result1.s_command, INVALID);
    ASSERT_EQ(result2.s_command, INVALID);
 }

 // OK IF there's an integer > 0:

 TEST(command_parse, SETRUN_OK) {
    std::string line1("SETRUN 1");
    std::string line2("setrun 2");

    auto result1 = parseCommand(line1);
    auto result2 = parseCommand(line2);

    ASSERT_EQ(result1.s_command, SETRUN);
    ASSERT_EQ(result2.s_command, SETRUN);

    ASSERT_EQ(result1.s_intarg, 1);
    ASSERT_EQ(result2.s_intarg, 2);
 }

 TEST(command_parse, SETRUN_BAD_NUMBER) {
    // The run number must be > 0:

    std::string line("setrun 0");

    auto result = parseCommand(line);
    ASSERT_EQ(result.s_command, INVALID);
 }
 TEST(command_parse, SETRUN_EXTRA_TAIL) {
    // no tail after run numbger:

    std::string line("setrun 1 bad");
    auto result = parseCommand(line);
    ASSERT_EQ(result.s_command, INVALID);
 }