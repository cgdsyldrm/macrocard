# PlatformIO pre-build script: some pioarduino installs leave the compiler in
# <toolchain>/xtensa-esp-elf/bin instead of <toolchain>/bin, which PlatformIO
# does not put on PATH. Add the nested bin folder when that happens.
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

platform = env.PioPlatform()  # noqa: F821
for name in ("toolchain-xtensa-esp-elf",):
    pkg_dir = platform.get_package_dir(name)
    if not pkg_dir or os.path.isdir(os.path.join(pkg_dir, "bin")):
        continue
    nested = os.path.join(pkg_dir, "xtensa-esp-elf", "bin")
    if os.path.isdir(nested):
        env.PrependENVPath("PATH", nested)  # noqa: F821
        print("toolchain_path: using", nested)
