set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_FLAGS_INIT "-m32 -march=i686")
set(CMAKE_CXX_FLAGS_INIT "-m32 -march=i686")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
# Build within Debian 12 i386 (recommended for releases), or install multilib
# and 32-bit development libraries when cross-building from an x64 Linux host.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig")
