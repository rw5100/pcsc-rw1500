# Linux x86_64 binary

The GitHub Actions artifact contains an IFD bundle with librw5100 statically
embedded. libusb and the system C runtime remain dynamic dependencies.
Build baseline: Ubuntu 22.04, glibc 2.35. This is not an Alpine/musl binary.
Install pcscd and the libusb runtime using your distribution's package manager.

Verify SHA256SUMS before extracting the archive. Copy the whole
ifd-rw5100.bundle directory into your pcsc-lite USB driver directory; obtain
the directory with `pkg-config --variable=usbdropdir libpcsclite` when the
development package is installed. Do not assume all distributions use the
same path. Restart pcscd after installation, then run pcsc_scan with a card.

If pcscd runs unprivileged, grant its service account access only to USB
04dd:9259 through a udev rule. On NixOS use the Nix package and
services.pcscd.plugins instead of copying files into /nix/store.

CI tests library and adapter behavior, loads the actual ELF plugin and checks
exports, dependencies and GLIBC symbol versions. Physical card communication
and distribution-specific service integration still require hardware testing.
Artifacts are downloadable from the workflow run for 30 days. Pushing a v*
tag publishes a GitHub Release with the archive and SHA256SUMS after the
build and checks pass. Ordinary branch pushes and pull requests do not publish.
