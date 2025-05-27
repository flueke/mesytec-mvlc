/*
  @file CVMEModule.h
  @brief Header for generic slow controls VME access.
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2025.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
	     NSCL
	     Michigan State University
	     East Lansing, MI 48824-1321
*/

#ifndef MVLC_CVMEMODULE_H
#define MVLC_CVMEMODULE_H

#include "SlowControlsDriver.h"
#include "SlowControlsModuleCommand.h"
#include <string>
#include <vector>
#include <stdint.h>

namespace mesytec {
  namespace mvlc {
    class MVLC;
    enum class VMEDataWidth;
  }
}
class CTCLObject;


/**
 * CVMEModule allows arbitrary  VME access by a client.  The idea is that a Set command
 * Will send a list of operations to perform.  These operations can be any mix of read and
 * write operations.  This is intended to be used with the CVMERemote class which is closely
 * follows the VMUSBReadoutList class to enhance portability with VMUSBReadout clients.
 * 
 * The module only performs Set commands and the form of those are of the form:
 * 
 * ```tcl
 *     Set list list-of-operations
 * ```
 *  Where list-of-operations is a Tcl formatted list of operations. The list consist of a set of sublists.
 * Each sublist specifies a read or write to/from the VME as follows:
 * 
 * - Read operations are of the form {r amod address width}  Where r is a literal r indicating the
 *   operation is a read operation.  amod is an integer address modifier, address the address of the
 *   to be read and width one of 16 or 32 indicating the width of the read.
 * - Write operations are of the form {w amod address data width} where w is a literal w and data are the
 *   data to write.
 * 
 * Note this is not compatible with the VMUSB module in VMUSBReadout as its list are raw VMUSB opcodes.
 *
 * Success will return:
 *  OK hexadecimalized-output-buffer
 * Where
 *  - hexadecimalized-output-buffer is the reply buffer converted to a hexadecimal representation
 *    of each byte as a Tcl list.  Note that if no output data are available, and empty list will
 *    be returned.  This is fully compatible with the VMUSB client.
 *
 * Note that this function can therefore also provide all single shot operations as those are just
 * lists with one element.
 */
class CVMEModule : public SlowControlsDriver
{
  // Private data structures - these are the compiled
  // list:
private:
  enum OpType {Read, Write};   
  typedef struct _VmeOperation {
    OpType             s_op;
    uint8_t            s_modifier;
    uint32_t           s_address;
    uint32_t           s_data;             // Only meaningful on Write ops.
    mesytec::mvlc::VMEDataWidth s_width;
  } VmeOperation;

  // Canonical operations:
public:
  CVMEModule(mesytec::mvlc::MVLC* controller);
  virtual ~CVMEModule();

  // Forbidden canonicals:
private:
  CVMEModule(const CVMEModule& rhs);
  CVMEModule& operator=(const CVMEModule& rhs);
  int operator==(const CVMEModule& rhs)const;
  int operator!=(const CVMEModule& rhs)const;

  // Virtual overrides:
  
public:
  virtual std::string Set(const char* pv, const char* value) ;
  virtual std::string Get(const char* pv) ;
  virtual void Update() ;

  // private utilities:

  // Text list into VME operations:

  std::vector<VmeOperation> CompileList(const char* ops);
  VmeOperation              CompileItem(CTCLObject& op);
  mesytec::mvlc::VMEDataWidth decodeWidth(CTCLObject& wid);
  // The actual operations.

  void                      execWrite(const VmeOperation& op);
  uint32_t                  execRead(const VmeOperation& op);
  std::string               createOkResponse(const std::vector<uint32_t> readData);
};

#endif
