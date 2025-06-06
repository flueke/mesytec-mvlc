### Introduction

This directory supplies an optional program, fribdaq-readout that is based on minidaq, but can write data to an FRIB/NSCLDAQ ring buffer.  To enable building and installing the program you must add

```
 -DNSCLDAQ_ROOT=nscldaqroot
```

where ```nscldaqroot``` is the top level directory of the FRIB/NSCLDAQ installation you want this to link against.  The softare was tested with FRIB/NSCLDAQ version ```12.2```,  that is the minimum NSCLDAQ version it
can be built with.

#### Prerequisite packages.
Building the fribdaq-readout program also requires the Tcl development package.

*  For Debian based distributions  this is called tcl-dev
*  I believe for RHEL and Fedora systems this is called tcl-devel.

If you are using the FRIB containerized environment this will have been installed in the container image.


### Using fribdaq-readout

```fribdaq-readout``` supports a reduced set of the minidaq options.  Several options that don't make
sense in the context of the FRIB/NSCLDAQ environment have been removed.
(use --help to list the full option set and short form documentation)  with the added options:

* ```--ring ringname```  selects the name of the ring buffer to which the data should be written.  This is a ring name not a URI, as only local ringbuffers can be written to.  If the option is not provided, this defaults to the name of the logged in user.
* ```--sourceid id``` When used in a larger system that builds events from several sources using the FRIB/NSCLDAQ event builder, this specifies the unique integer source id  that will be used to identify fragments from this data source.   If not provided, this defaults to 0.
* ```--timestamp-library``` When used with a larger system that builds events from several sources using the FRIB/NSCLDAQ event builder, this specifies a shared library file which includes code to extract timestamps from the raw event data.   If not provided, the body headers required to build events will not be included in the ```PHYSCIS_EVENT``` items.

Following all options on the command line, a single positional parameter provides the name of a .yaml configuration file that was either exported from ```mdaq``` or produced using the FRIBDA/NSCLDAQ configuration tools mvlcgenerate in FRIB/NSCLDAQ version 12.2 and later.

####  fribdaq-readout commands

The program accepts all of the commands that a FRIB/NSCLDAQ readout framwork does.  It runs a Tcl interpreter as well.  

Command:

* begin
* end
* pause
* resume
* runstat
* init

Special Variables:

* title - has the title that will be assigned to the next run.
* run   - Has the run number that will be assigned to the next run.
* state - has the current runstate, one of ```idle```, ```active``` or ```paused```

Note that changes to the title an run variables while the run is not ```idle``` can be made but have no
effect on the run number and title of the run.

#### Creating setups with mdaq.

The fribdaq-readout program makes some assumptions about the configuration it has been given:

* Stack 0 in mdaq is assumed to be PHYSICS_EVENT data.  It will generate PHYSICS_EVENT ring items.
* Stack 1 is assumed to be data from periodic scalers.  It will generate PERIODIC_SCALER ring items.

FRIB/NSCLDAQ setup software will generate appropriate stack configurations with the event trigger a pulse in NIM1.

Note that if you use mdaq you should remove the MCST sections of the configurations as multiple instances will cause start errors at this time.

### Slow controls server

The program supports the standard FRIB/NSCLDAQ slow contols server protocol with drivers ported from the VMUSBReadout program.  The server is enabled by specifying the ```--controlport``` option which takes a numeric TCP/IP server port number.  When this is supplied you must also provide a ```--ctlconfig``` which specifies a Tcl script that is executed by the control server on startup to define the driver instances and their configuration.  

#### Server configuration

The server is configured using the script specified by ```--ctlconfig```  This is a Tcl script. Driver intsances are created using the ```Module``` command.  See below.

#### Server requests

Server requests are text strings, that are actually Tcl commands executed by the server in a safe interpreter.
The commands include:

*  Module - a command ensemble that allows you to create and configure driver instances as well as instrospect the driver instances that have been created, and their configurations
and the driver types that are supported.
*  Set  - set the parameter of a device instance.
*  Get  - get the value of a device instance.
*  Update - Update knowledge of the device and restore its settings.
*  mon - Retrieve monitorable parameters.

The Module command is normally not used as a server command but it is recognized as such if dynamic configuration is desired.  It supports the subcommands:

* create - takes a device type an a unique instance name for the driver instance it will create e.g.
```Module create test testinstance```
* config - Takes a device instance and a set of configuration option name/values.  e.g.
```Module config testinstance -parameter 12```
*  cget - gets the configuration of an device instance.  If only the instance name is provided,
the entire configuration is returned a name value pairs and if a config option is provided only
that value is returned e.g. ```Module cget testinstance``` returns the entire configuration while
```Module cget testinstance -parameter``` returns only the value of the -parameter configuration option.
* list -takes an optional pattern that defaults to * if not provided.  Returns a (possibly) empty list of pairs whose instance names match the pattern.  Each pair contains, in order, the instance name and type.
* types - Takes an optional pattern, that defaults to *.  Returns a list of module types that are defined and match the pattern.

#### Supported slow controls driver types

This list of driver types is recognized by the ```Module create``` command:

*  test - a test driver intended for use by me.
*  jtecgdg - the Jtec gate and delay generator.
*  v812 - The CAEN V812 and V895 discrinmnators.
*  jtecgdg - Gate and delay generator from JTEC.
*  mxdcrcbus - Mesytec RC bus controlled by e.g. MADC32 and similar devices.
*  mxdcreset - Reset the RC bus controlled by e.g. MADC32 and similar when updated.
*  v6533 - CAEN V6533 HV controller.
*  tcl  - Wrapper for Tcl slow controls drivers.
*  vme, vmusb - generic VME operations the type vmusb is provided for compatibility with the VMUSBReadout 
slow controls system but is the same driver.

#### Writing C++ slow controls drivers and incorporating them 

Slow controls drivers are classes derived from SlowControlsDriver which is defined in 
```<fribdaq/SlowControlsDriver.h>```.  They implement the following methods:

*  A constructor that takes a mestyec::mvlc::MVLC* as a parameter and invokes the base class constructor with 
that parameter.  The controller is then available in the protected member m_pVme.
*  ```std::string Set(const char* pname, const char* value)``` - Sets a parameter the driver controls named
```pname``` to the value ```value``` The return on success should be "OK" else "ERROR - \<some error message>"
*  ```std::string Get(const char* pname)```  Returns the value of the parameter pname.  The result string should be one of "OK value"  or "ERROR - \<some error message>"
* ```void Update()``` - Update the device with whatever internally stored values the driver has.
* ```void reconfigure()``` (optional) - Called when the options for the driver have been reconfigured (see below).
* ```std::string getMonitor()``` (optional) - Called when the client wants to get data that is worth monitoring (e.g. currents and trips for an HV driver).  The return value is "OK \<data>" on success or
"ERROR - \<error message>"  If the data could not be gotten.  the data returned can be in any format defined by the driver, however a well formatted Tcl list might be a good choice given the Tcl control panel clients
often used.


##### Installing and configuration.

Associated with each driver is a creator (derived from SlowControlsCreator  defined in 
```<fribdaq/SlowControlsModuleCommand.h>```), and registration of the driver in the set of driver types
understood by the ```Module``` configuration command.  The SlowControlsDriver base class includes an instance 
of an ```XXUSB::CConfigurableObject```.  A pointer to that object can be gotten via the public
method ```getConfiguration```.  The creator is, when creating a driver instance, expected to define the configuration options the driver will respond to when ```Module config``` is used to configure it.

Here is an example of the slow controls test driver's creator and registration:

```c++
// From SlowControlsTestDevice.h
...
class SlowControlsTestCreator : public SlowControlsCreator {
public:
    virtual SlowControlsDriver* create(mesytec::mvlc::MVLC* controller);
    class Register {
    public: 
        Register();
    };
};
```

The creator creates the device driver intance and tells the configuration to accept a configuration option
```-parameter``` which must be an integer in the range [0,65535], with an initial value of 0:

```c++
// From SlowControlsTestDevice.cc:

...
SlowControlsDriver*
 SlowControlsTestCreator::create(MVLC* controller) {
    auto result = new SlowControlsTestDriver(controller);

    CConfigurableObject* pConfig = result->getConfiguration();
    pConfig->addIntegerParameter("-parameter", 0, 65535, 0);   // 16 bit integer value.

    return result;
 }
 ```

 The creator defined a public subclass named Register.  The constructor of that class will 
 registesr the creator with the factory used by the ```Module create``` command to create an appropriate
 driver instance for the type specified.  Here's its implementation:

 ```c++
 // From SlowControlsTestDevice.cc:

SlowControlsTestCreator::Register::Register() {
    SlowControlsFactory::getInstance()->addCreator("test", new SlowControlsTestCreator);
}
```
All it does is locate the slow control device factory and register the SlowControlsTestCreator to responde to the device type ```test```

Registratation is then a simple matter of:

```c++
// From SlowCOntrolsTestDevice.h
static SlowControlsTestCreator::Register autoregister;  
```

You can build C++ drivers in shared objects and incorporate them into the system via the Tcl ```load``` command in your controls configuration script.

#### Writing Tcl slow controls drivers and incorporating them

The ```tcl``` driver type allows slow control modules to be written in Tcl. A slow controls driver instance must be a Tcl command ensemble.  These are most simply written in TclOO, snit or itcl.  They are compatible with VMUSBReadout Tcl slow controls drivers.  They must implement the subcommands:

* Initialize - called to initialize access and the hardware. A VME access object base command is passed in.
* Update - Called to update the device from any internal values.  Passed a VME base command.
* Set - called to set a paramter passed a VME base command the name of the paramter and its desired value.
  Can return a string.  If the string begins  with ERROR, this is assumed to be an error. The subcommand should not producde an error.  If it does, the result is returned to the client preceded by "ERROR -" and error/traceback information is output to stderr.
* Get - called to return the value of a parameter.  Passed the VME base command and name of parameter to return.
* AddMonitorList - called to fetch a list of VME operations that will acquire monitored data.  If the list
is empty it's assumed no monitoring is being done.
* processMonitorList - Passed a list of bytes read by the monitor list.  These data should be prepared for:
* getMonitoredData - should return the monitored data passed in via processMonitorList in the manner expected by clients.

Below is a fragment from a control config script that defines an inline Tcl driver using snit, makes an instance and configures that instance with the base address of a CAEN V792 QDC.  When Get is requested to
return the "fw" parameter it returns the value of the firmware revision register.

The tcl driver is used to wrap this instance by specifying it as the ```-ensemble``` option.

```tcl
package require snit

snit::type TclDriver {
    variable saved_value  0
    option -base 0

    constructor args {
        $self configurelist $args
    }
    method Initialize vmusb {
    }
    method Update vmusb {
    }
    method Set {vmusb name value} {
        set saved_value $value
        return "Set $name to $value"
    }
    method Get {vmusb name} {
        if {$name eq "fw"} {
            set fwreg [expr {$options(-base) + 0x1000}]
            set result [$vmusb vmeRead16 $fwreg 0x09 ]
            return [format 0x%x $result]
        } else {
            return $saved_value
        }
    }
    method addMonitorList listobj {
    }

}

set instance [TclDriver %AUTO% -base 0x11040000]
Module create tcl mydriver
Module config mydriver -ensemble $instance
```

#### Using the generic vme/vmusb driver as a client.

The generic VME driver allows clients to perform arbitrary VME operations and retrieve the 
results of reads in those operations. It is registered to be created either as type ```vme``` or for compatiblity with VMUSBeadout control config scripts ```vmusb```:

For example:

```tcl
#  Fragment from ctlconfig.tcl
...
Module create vme vme;
...
```
The only parameter the driver accepts is ```list```.  The value list accepts is a Tcl list. Each element
is a sublist that specifies a VME opeation.  If the first element of the sublist is a "w", the operation is a write and the remaining are, in order, the address modifier, address, data and data width. For example
```
{w 0x09 0x1234000 0x666 16}
```
Does a 16 bit write of 0x666 to 0x12340000 with VME address modifier 0x09 (A32 non privileged space).
If the 16 was a 32 the write would be a 32 bit write.  If the first element of the sublist is a "r", the operation is a read and the remainder of the list are the address modifier, the address and width. For example,
to do a 16 bit read from the same address written above:

```
{r 0x09 0x12340000 16}
```

The returned value from the driver for a Set operation is a list of the values read for each read operations.

Both C++ and Tcl client software are provided for the generic VME driver.

##### C++ client class for the generic VME driver.
The C++ client class is called ```CVMEClient``` and is defined in \<fribdaq/CVMEClient.h>

The main parts of the definition are:

```c++
class CVMEClient {
// Public class types:

public:
    enum class DataWidth {D16, D32};       // D8 not allowed

public:
    CVMEClient(const char* host, const char* moduleName, uint16_t port);

public:
    int  addRead(uint32_t addr, uint8_t amod, DataWidth width);
    void addWrite(
        uint32_t addr, uint8_t amod, uint32_t data, DataWidth width
    );
    std::vector<uint32_t> execute();
    int readIndex(size_t operationIndex);

    void reset();
};
```
Construtting a client requires the host the client runs in, the name of the vme instance (last parameter of the Module create command that made it), and the port on which the slow controls server is listening.

The methods that do useful things are:

*  addRead - adds the specified read to the list of operations that are being accumulated. This takes
the address, address modifier and a width specification (e.g. CVMEClient::D32).
*  addWrite - adds the specified write to the list of operations being accumulaited.  This takes the address, address modifier, data to write and width specifier./
*  execute - requests the driver to perform the list of operations.  The return value are the data read.  Note that the list remains unmodified and an be run again via execute.
*  readIndex - returns the index in to the vector returned from execute at which the value read by the operationIndex'th element of the list will be found.  For example suppose you have a list that consists of three writes followed by a read.  readIndex(2) returns 0 indicating the data from that read will be found
in the first element of the list returned by execute().
*  reset - clears the list.  After this, readIndex will no longer be usable.

Here's a sample C++ client:
```
// vmeclientTest.cpp:

#include <fribdaq/CVMEClient.h>
#include <stdint.h>
#include <iostream>
#include <stdlib.h>
#include <stdexcept>

// Usage:  vmeclientTest v785|792|775-base-address
// Output will be the serial number, model and firmware rev.
//
//  We will first do a soft reset of the board. by flipping the
//  soft reset bit in bitset 1:

// Register offsets and defs.

static const uint32_t FIRMWARE(0x1000);
static const uint32_t BITSET1(0x1006);
static const uint32_t BITCLR1(0x1008);
static const uint32_t SOFT_RESET(0x80);  // reset bit in bitset/clear regs.

static const uint32_t BOARDID_LOW(0x803e);
static const uint32_t BOARDID_MID(0x803a);
static const uint32_t BOARDID_HIGH(0x8036);

static const uint32_t SERIAL_LOW(0x8f06);
static const uint32_t SERIAL_HIGH(0x8f02);

static const char* host("localhost");
static const char* name("vme");
static uint16_t port(27000);

int main(int argc, char** argv) {
    uint32_t base = strtoul(argv[1], nullptr, 0);
    std::cout << std::hex << "Base at 0x" << base << std::endl;

    CVMEClient client(host, name, port);

    auto d16 = CVMEClient::DataWidth::D16;

    // Reset the module:

    client.addWrite(base + BITSET1, 0x09, SOFT_RESET, d16);
    client.addWrite(base + BITCLR1, 0x09, SOFT_RESET, d16);

    // Read the firmware register.

    client.addRead(base + FIRMWARE, 0x09, d16); // 2.

    // The 2  bytes of the serial #:

    client.addRead(base + SERIAL_LOW, 0x09, d16); // 3
    client.addRead(base + SERIAL_HIGH, 0x09, d16);

    // The 3 bytes of model number:

    client.addRead(base + BOARDID_LOW, 0x09, d16); // 5
    client.addRead(base + BOARDID_MID, 0x09, d16);
    client.addRead(base + BOARDID_HIGH, 0x09, d16);


    std::vector<uint32_t> data;
    try {
        data = client.execute();
    }
    catch (std::exception& e) {
        std::cerr << "Exception from execute: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Firmware version:

    std::cout << "Firmware version:    " << data[client.readIndex(2)]
              << std::endl;

    // Assemble/output  the serial number:

    uint32_t serial =
        (data[client.readIndex(3)] & 0xff) | ((data[client.readIndex(4)] &0xff) << 8);
    std::cout << "Serial number:      " << std::dec << serial << std::endl;

    // Assemble/output the model number:

    uint32_t model =
        (data[client.readIndex(5)]  & 0xff) |
        ((data[client.readIndex(6)] & 0xff) << 8) |
        ((data[client.readIndex(7)] & 0xff) << 16);
    std::cout << "Model number:      " << model << std::endl;



    return 0;
}
```

To build this client:  g++ -o vmeclientTest -I$MVLCROOT/include -L$MVLCROOT/lib vmeclientTest.cpp -lslowControlsClient -Wl,-rpath=$MVLCROOT/lib

Where MVLCROOT is defined as the installation top directory of the mesytec-mvlc product.


#### Tcl client for the generic VME driver.

Tcl client software also can be used with the generic VME driver.  Thisi is available in the mvlcvme packagte
and closely  follows the C++ client code:
Thhe vme command ensemble creates and destroys client instances:

* vme create hostname instancename port - returns the name of a client instance which has subcommands that match the C++ client method names.
* vme destroy \<instancename> - destroys an client instance returned from vme create.

Here's a sample program that does the same thing as the C++ example:

```tcl
#tclclient.tcl

package require mvlcvme

set FIRMWARE 0x1000
set BSET1   0x1006
set BCLR1   0x1008
set SOFT_RESET 0x80

set IDLOW 0x803e
set IDMID 0x803a
set IDHI  0x8036

set SNUMLOW 0x8f06
set SNUMHI  0x8f02


set base [lindex $argv 0]
puts "Base address [format %x $base]"

set crate [vme create localhost vme 27000]

#  Reset board:

$crate addWrite [expr $base + $BSET1] 0x09 $SOFT_RESET 16
$crate addWrite [expr $base + $BCLR1] 0x09 $SOFT_RESET 16

set fw [$crate addRead [expr $base + $FIRMWARE] 0x09  16]

# 3,4 - serial #
$crate addRead [expr $base + $SNUMLOW] 0x09 16
$crate addRead [expr $base + $SNUMHI] 0x09 16

# 5, 6, 7 - model

$crate addRead [expr $base + $IDLOW] 0x09 16
$crate addRead [expr $base + $IDMID] 0x09 16
$crate addRead [expr $base + $IDHI]  0x09 16

set data [$crate execute]

set firmware [lindex $data $fw]

puts "Firmware: [format %x $firmware]"

set snolo [lindex $data [$crate readIndex 3]]
set snohi [lindex $data [$crate readIndex 4]]

set sno [expr ($snolo & 0xff) | (($snohi & 0xff) << 8)]
puts "Serialno:  $sno"

set modlo [lindex $data [$crate readIndex 5]]
set modmid [lindex $data [$crate readIndex 6]]
set modhi  [lindex $data [$crate readIndex 7]]

set modno [expr ($modlo & 0xff) | (($modmid & 0xff) << 8) | \
               (($modhi & 0xff) << 16)]

puts "Model : $modno"

```

To run this:

TCLLIBPATH=$MVLCROOT/lib tclsh tclclint.tcl \<base-address>  

where \<base-address> is the base address of a CAEN e.g. V792, V785, V775 module.
and MVLCROOT is defined as the top level directory in which the mesytec-mvlc product is installed.


#### Using fribdaq-readout with the FRIB/NSCLDAQ event builder.



To use the fribdaq-reaout with the FRIB/NSCLDAQ event builder, you must supply code to extract timestamps from the data.   This code is dynamically loaded from a shared object.  The shared library must have a  C bindings entry point named ```extract_timestamp```
that returns a uint64 timestamp.  Input parameters are:
*  unsigned nmodules The number of module data structs in event below,
*  const mesytec::mvlc::readout_parser::ModuleData* event a pointer to the first module data in the event.

Here is a sample timestamp extractor that just returns an incrementing trigger number:

```c++
#include <fribdaq/parser_callbacks.h>
#include <stdint.h>
// Timestamp extractors must have C linkage:
extern "C" {
    /** Sample timestamp extractor
        normal extractors need to look at the data which are
        in the module structs
     */
    uint64_t
    extract_timestamp(unsigned numModules, const mesytec::mvlc::readout_parser::ModuleData* event) {
        static uint64_t timestamp = 0;

        return timestamp++;
    }
}
```

*  Note the use of extern "C" to force the bindings to C rather than C++ bindings.  If you actually compile your extractor in C you should not do this.
*  The parser_callbacks.h provides definitions need by the parser callbacks and also pulls in the Mesytec headers that define the ModulData structure.  Module data supplies the data for each module in the mdaq configuration in order from first to last.  It has a few fields but the ones you usually care about are:
    * size - which is th number of uint32_t items in the data for this module
    * data - a void pointer to the data read from the module.

In a typical actual extractor the first 64 bits of the first module will typically be the timestamp. so the last line of the example might be:
```c++
...
    const uint64_t* pTs = reinterpret_cast<const uint64_t*>(event->data);
    return *pTs;
...
```

#### Event format.

Recall that stack 0 is used by fribdaq-readout for event data.  It is packaged in a ringbuffer using a normal CPhysicsEventItem.  If a timestamp extractor is provided, the item will have a body header.  If not it won't.  The body of the event will consist just of the data from each ModuleData packed end-to-end in the payload of the ring item.

Here is the code fragment that creates and submits ring items:

```c++

...
std::unique_ptr<CPhysicsEventItem> pEvent;
    if (context->s_tsExtractor) {
        pEvent.reset(new CPhysicsEventItem(
            context->s_tsExtractor(moduleCount, pModuleDataList), context->s_sourceid, 0,
            eventSize + 100
        ));
    } else {
        pEvent.reset(new CPhysicsEventItem(eventSize + 100));
    }
    CPhysicsEventItem& event(*pEvent);   

    for ( int i  = 0; i < moduleCount; i++) {
        uint32_t* pCursor = reinterpret_cast<uint32_t*>(event.getBodyCursor());
        auto size = pModuleDataList->data.size;
        memcpy(pCursor, pModuleDataList->data.data, size * sizeof(uint32_t));
        pCursor += size;
        event.setBodyCursor(pCursor);
    }
    event.updateSize();

    event.commitToRing(*(context->s_pRing));
...
```

where context is a struct that contains information like the source id, the ring item, and the timestamp extractor function pointer.

See the FRIB/NSCLDAQ documentation for CPhysicsEventitem for more information.   

The work of putting data into the ring item body  is done in the for loop which iterates over the modules ModuleData items passed in and copies the data associated with each module into the ring item body.  Note that the size of the body can be gotten from a reconstituted object by calling the object's getBodySize method.  


