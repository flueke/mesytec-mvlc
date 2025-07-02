

#ifndef VMEADDRESSMODIFIERS_H
#define VMEADDRESSMODIFIERS_H

#include <stdint.h>

namespace VMEAMod 
{

const uint8_t a64MultiBlock       = 0x00;
const uint8_t a64Data             = 0x01;
const uint8_t a64Block            = 0x03;
const uint8_t a64Lock             = 0x04;
const uint8_t a32Lock             = 0x05;

const uint8_t a32UserMultiBlock   = 0x08;
const uint8_t a32UserData         = 0x09;
const uint8_t a32UserProgram      = 0x0a;
const uint8_t a32UserBlock        = 0x0b;

const uint8_t a32PrivMultiBlock   = 0x0c;
const uint8_t a32PrivData         = 0x0d;
const uint8_t a32PrivProgram      = 0x0e;
const uint8_t a32PrivBlock        = 0x0f;

const uint8_t a2eVME6U            = 0x20;
const uint8_t a2eVME3U            = 0x21;

const uint8_t a16UserData         = 0x29;
const uint8_t a16Lock             = 0x2c;
const uint8_t a16PrivData         = 0x2d;

const uint8_t aCRCSR              = 0x2f;

const uint8_t a24Lock             = 0x32;
const uint8_t a40Access           = 0x34;
const uint8_t a40Lock             = 0x35;
const uint8_t a40Block            = 0x37;

const uint8_t a24UserMultiBlock   = 0x38;
const uint8_t a24UserData         = 0x39;
const uint8_t a24UserProgram      = 0x3a;
const uint8_t a24UserBlock        = 0x3b;

const uint8_t a24PrivMultiBlock   = 0x3c;
const uint8_t a24PrivData         = 0x3d;
const uint8_t a24PrivProgram      = 0x3e;
const uint8_t a24PrivBlock        = 0x3f;

} // end namespace
#endif
