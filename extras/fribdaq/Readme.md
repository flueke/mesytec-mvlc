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

```fribdaq-readout``` supports the options supported by minidaq
(use --help to list the full option set and short form documentation)  with the added options:

* ```--ring ringname```  selects the name of the ring buffer to which the data should be written.  This is a ring name not a URI, as only local ringbuffers can be written to.  If the option is not provided, this defaults to the name of the logged in user.
* ```--sourceid id``` When used in a larger system that builds events from several sources using the FRIB/NSCLDAQ event builder, this specifies the unique integer source id  that will be used to identify fragments from this data source.   If not provided, this defaults to 0.
* ```--timestamp-library``` When used with a larger system that builds events from several sources using the FRIB/NSCLDAQ event builder, this specifies a shared library file which includes code to extract timestamps from the raw event data.   If not provided, the body headers required to build events will not be included in the ```PHYSCIS_EVENT``` items.

Following all options on the command line, a single positional parameter provides the name of a .yaml configuration file that was either exported from ```mdaq``` or produced using the FRIBDA/NSCLDAQ configuration tools (to be included in versions 12.2 of FRIB/NSCLDAQ).

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


