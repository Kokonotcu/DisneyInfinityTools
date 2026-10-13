cmake_minimum_required(VERSION 3.21)
project(di1compat C CXX)
# x86 only: the game is 32-bit.  cmake -S . -B <build> -G "Visual Studio 17 2022" -A Win32
add_library(di1compat SHARED
    di1compat.cpp di1compat.def
    minhook/buffer.c minhook/hook.c minhook/trampoline.c minhook/hde/hde32.c
    third_party/miniz/miniz.c)
target_compile_features(di1compat PRIVATE cxx_std_17)
set_property(TARGET di1compat PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")   # no VC runtime DLL dependency
