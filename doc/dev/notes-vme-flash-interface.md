Notes from debugging and working with the VME Flash interface using an MDPP-16
==============================================================================

* Response to set area:
  - mirror of the opcode and args, 4 words
  - 0xff, 0xf, then InvalidRead is set
  => Have to read 6 words, the 7th will have the InvalidRead set

* Flash access is exclusive: either mini-usb or vme.
  If mini-usb was last used then flash operations won't succeed.
  If flash is enabled via 0x6200 then mini-usb operations will time out in mvp.


