# pcsc-rw5100

IFD Handler 3.0 plugin for the Sharp/Sanwa RW5100 (`04dd:9259`), using
[librw5100](https://git.huggy.moe/rw5100/librw5100). Applications use the
system PC/SC API; the plugin handles reader access and passes APDU responses
unchanged. Card-specific data decoding belongs to the application.

The project produces `ifd-rw5100.bundle` for Linux pcsc-lite and macOS.
Windows native PC/SC requires a different reader-driver integration.

## Build

With librw5100 installed and discoverable through pkg-config:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Linux additionally needs the pcsc-lite development package. macOS uses the
PCSC framework provided by the SDK. The IFD ABI header is vendored; see
[third-party notice](NOTICE.md).

To develop against the adjacent library source without installing it:

```sh
nix-shell -p cmake pkg-config libusb1 --run 'cmake -S . -B build -DRW5100_SOURCE_DIR="$PWD/../librw5100" -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure'
```

`RW5100_SOURCE_DIR` is optional. Build products stay in the ignored `build/`
directory. The bundle links its dependencies from the selected build
environment; a Nix development build retains a Nix-store libusb dependency.
For distribution, supply the dependency at its linked location or package
and sign it appropriately. The macOS build ad-hoc signs the entire bundle;
this is local test signing, not Developer ID signing or notarization.

## Installation and system verification

Linux defaults to pcsc-lite's pkg-config `usbdropdir`. macOS defaults to
`/usr/local/libexec/SmartCardServices/drivers`. Override with
`-DPCSC_DRIVER_DIR=/your/driver/directory` when configuring.

```sh
sudo cmake --install build --component pcsc
```

On macOS, the equivalent install for an already built bundle is:

```sh
sudo /usr/bin/ditto build/ifd-rw5100.bundle /usr/local/libexec/SmartCardServices/drivers/ifd-rw5100.bundle
```

On macOS, close PC/SC clients, then stop the cached broker after installing a new plugin:

```sh
sudo /usr/bin/killall -KILL com.apple.ifdreader
```

The broker caches the driver index once per process; replugging alone does
not refresh that index. Restarting briefly interrupts PC/SC clients, so close
those clients first. Replug the reader to let launchd start a fresh broker.
On macOS 15.7.7, `launchctl kickstart` is blocked by SIP (error 150);
changing SIP settings is not part of installation.
The system broker does not scan an
arbitrary user-local build directory. On Linux, use the distribution's
pcscd service and USB permissions. Do not run the standalone library CLI
or direct IFD check while the PC/SC broker owns this reader.

```sh
./build/rw5100-pcsc-check
```

This real PC/SC client enumerates readers, selects exactly one name containing
`RW5100` (or the exact name supplied as an argument), connects, reports ATR,
and sends three read-only SELECT MF commands in a transaction. It preserves
and prints status words; `6A81` is a card reply, not a USB transport failure.
It then releases the transaction and disconnects with power-down.

For direct plugin diagnostics **before system installation**:

```sh
# macOS; replace MacOS with Linux on Linux
./build/ifd-check ./build/ifd-rw5100.bundle/Contents/MacOS/ifd-rw5100
```


## Scope and recovery

One card slot per reader; up to 16 readers with distinct explicit USB selectors.
Generic VID/PID-only opening requires exactly one reader. macOS supplies
the plist friendly name, which also requires exactly one connected RW5100. Linux libudev and
libusb-1.0 names select bus/address/interface 0. Unknown selectors are rejected
rather than opening an arbitrary device. Access is serialized, including
presence polling. No PIN-pad, escape/control commands or custom PTS settings
are advertised. T=0 support inherits the library's short-APDU limitations.

The IFD layer maps SetProtocol's PC/SC masks 1/2 and Transmit's protocol
numbers 0/1 explicitly. It returns zero receive length on errors and does not
replay APDUs after a timeout or insufficient buffer. After transport loss,
close/reopen the reader channel; a card reset alone cannot repair a poisoned
USB connection. Physical hotplug handling is owned by the resource manager.

## Linux binary artifacts

The Linux x86_64 GitHub Actions workflow runs on push, pull request and manual
dispatch. Download the plugin archive and SHA256SUMS from its artifacts.
It embeds librw5100 and uses system libusb/glibc; see
[Linux binary installation](docs/linux-binary.md) for requirements.
