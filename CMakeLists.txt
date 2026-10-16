cmake_minimum_required(VERSION 3.16)
project(my-client CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Vendored rdplib: the recovered EQ RDP transport (MIT), copied from EQMacEmu's server/submodules/rdplib.
set(RDPLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/rdplib)

find_package(OpenSSL REQUIRED)

add_executable(eqclient
    src/main.cpp
    src/rdp_session.cpp
    src/login_client.cpp
    src/world_client.cpp
)
target_link_libraries(eqclient PRIVATE rdplib OpenSSL::Crypto)
# DES_* is deprecated in OpenSSL 3 but is exactly what the 2002 client uses.
target_compile_options(eqclient PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
