# ARM 交叉编译工具链 (STM32MP157)
# 使用方法: cmake -DCMAKE_TOOLCHAIN_FILE=scripts/arm_toolchain.cmake ..
#
# 必须使用 Linaro gcc 7.5.0 (sysroot glibc 2.25)
# apt 的交叉编译器 glibc 2.39 与开发板 glibc 2.28 不兼容!

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_ROOT /opt/toolchain/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Cortex-A7 NEON 优化
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING "")
