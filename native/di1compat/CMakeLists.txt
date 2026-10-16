cmake_minimum_required(VERSION 3.21)
project(di1compat C CXX)
# x86 only: the game is 32-bit.  cmake -S . -B <build> -G "Visual Studio 17 2022" -A Win32
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "di1compat is 32-bit only: configure with -A Win32")
endif()
# CMake's default /EHsc stays: catch (...) never sees an SEH fault, only __try does (docs/audit OT-36).
add_library(di1compat SHARED
    di1compat.cpp di1compat.def
    minhook/buffer.c minhook/hook.c minhook/trampoline.c minhook/hde/hde32.c
    third_party/miniz/miniz.c)
target_compile_features(di1compat PRIVATE cxx_std_17)
set_property(TARGET di1compat PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")   # no VC runtime DLL dependency
# octtest: the offline test driver (modes in octtest.cpp's main); it keeps the VC runtime DLL
add_executable(octtest octtest.cpp third_party/miniz/miniz.c)
target_compile_features(octtest PRIVATE cxx_std_17)
# /W4 on our C++ only; minhook and miniz keep the default level. Warnings, not errors.
set_source_files_properties(di1compat.cpp octtest.cpp PROPERTIES COMPILE_OPTIONS /W4)
# cmake --install <build> --config Release --prefix "<3.0 install>" copies the DLL into Pentane\Plugins
install(FILES $<TARGET_FILE:di1compat> DESTINATION Pentane/Plugins)
