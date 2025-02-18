# Specify the target system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify the cross-compilers
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Set architecture-specific flags based on your Docker settings
set(CMAKE_C_FLAGS "-march=armv7 -mtune=cortex-a8 -mfpu=neon" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-march=armv7 -mtune=cortex-a8 -mfpu=neon" CACHE STRING "" FORCE)

# Configure CMake to look for libraries and headers in the target system
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
