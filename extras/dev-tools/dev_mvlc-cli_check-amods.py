#!/usr/bin/env python3

# Uses mvlc-cli to issue vme_read requests. Cycles through the list of amods,
# pauses after each command.  Used to verify the amods are correctly transmitted
# over the VME Bus by using this script with a VME bus analyzer in the crate.
# Note: no module with the target address was present in the crate. All reads
# timed out.

import subprocess

class VMEAddressModifiers:
    # a32
    a32UserData    = 0x09
    a32UserProgram = 0x0A
    #a32UserBlock   = 0x0B
    #a32UserBlock64 = 0x08

    a32PrivData    = 0x0D
    a32PrivProgram = 0x0E
    #a32PrivBlock   = 0x0F
    #a32PrivBlock64 = 0x0C

    # a24
    a24UserData    = 0x39
    a24UserProgram = 0x3A
    #a24UserBlock   = 0x3B

    a24PrivData    = 0x3D
    a24PrivProgram = 0x3E
    #a24PrivBlock   = 0x3F

    cr             = 0x2F

mvlc_cli="/home/florian/src/mvme2/build/mvlc-cli"
target_address=0x55551111
mvlc="mvlc-0066"

# Example mvlc-cli invocation:
# ./mvlc-cli --mvlc mvlc-0066 vme_read --amod 0x20 --datawidth=d32 0x12346008


# Get list of (name, amod) pairs
amods = [(name, getattr(VMEAddressModifiers, name))
         for name in dir(VMEAddressModifiers) if not name.startswith('_')]

# Run mvlc-cli for each amod
for name, amod in amods:
    print(f"\n=== Testing {name}: 0x{amod:02X} 0b{amod:06b} ===")
    cmd = [mvlc_cli, "--mvlc", mvlc, "vme_read", "--amod", f"0x{amod:02X}", "--datawidth=d32", f"0x{target_address:08X}"]
    print(cmd)
    subprocess.run(cmd)
    input("Press Enter to continue...")
