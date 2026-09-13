# Third-party source

`src/ifdhandler.h` is from pcsc-lite 2.4.1:
https://github.com/LudovicRousseau/PCSC/blob/2.4.1/src/PCSC/ifdhandler.h

Its copyright notices, BSD license conditions and disclaimer are retained in
the file. Local changes select the Apple PCSC framework includes and preserve
Apple's 32-bit RESPONSECODE definition on macOS. On Linux, installed
pcsc-lite headers provide native ABI types.

The plugin uses librw5100 and libusb; consult their distributions for their
respective license terms.
