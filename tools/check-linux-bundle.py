"""Validate the actual Linux release module without requiring a reader."""
import ctypes
import pathlib
import re
import subprocess
import sys

module = pathlib.Path(sys.argv[1]).resolve() / "Contents/Linux/ifd-rw5100"
header = subprocess.check_output(["readelf", "-h", str(module)], text=True)
assert "ELF64" in header and "Advanced Micro Devices X86-64" in header, header
dynamic = subprocess.check_output(["readelf", "-d", str(module)], text=True)
assert "librw5100" not in dynamic, "librw5100 must be statically embedded"
assert "/nix/store" not in dynamic, "Nix runtime dependency in release module"
assert "(RPATH)" not in dynamic and "(RUNPATH)" not in dynamic, dynamic
versions = subprocess.check_output(["readelf", "--version-info", str(module)], text=True)
required = {tuple(map(int, v.split('.'))) for v in re.findall(r"GLIBC_([0-9.]+)", versions)}
assert required and max(required) <= (2, 35), required
dependencies = subprocess.check_output(["ldd", str(module)], text=True)
assert "not found" not in dependencies, dependencies
assert "/nix/store" not in dependencies, dependencies
driver = ctypes.CDLL(str(module))
for symbol in (
    "IFDHCreateChannel", "IFDHCreateChannelByName", "IFDHCloseChannel",
    "IFDHGetCapabilities", "IFDHSetCapabilities", "IFDHSetProtocolParameters",
    "IFDHPowerICC", "IFDHTransmitToICC", "IFDHControl", "IFDHICCPresence",
):
    getattr(driver, symbol)
print("Linux x86_64 module loads; IFD exports and dependency checks passed")
