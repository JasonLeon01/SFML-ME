# OpenHarmony/HarmonyOS mobile build contract.
#
# The official DevEco toolchain owns the compiler, sysroot, ABI and C++ runtime
# selection. This module validates that contract and configures SFML-specific
# policy after GNUInstallDirs has initialized the install directory variables.

include_guard(DIRECTORY)

set(_SFML_HARMONY_MIN_SDK_API 21)

sfml_set_option(SFML_HARMONY_DEVICE_FORM MOBILE STRING "OpenHarmony device form (only MOBILE is currently implemented)")
string(TOUPPER "${SFML_HARMONY_DEVICE_FORM}" SFML_HARMONY_DEVICE_FORM)
set(SFML_HARMONY_DEVICE_FORM "${SFML_HARMONY_DEVICE_FORM}" CACHE STRING
    "OpenHarmony device form (only MOBILE is currently implemented)" FORCE)
set_property(CACHE SFML_HARMONY_DEVICE_FORM PROPERTY STRINGS MOBILE 2IN1)

if(SFML_HARMONY_DEVICE_FORM STREQUAL "2IN1")
    message(FATAL_ERROR "SFML_HARMONY_2IN1 is reserved for a future desktop OpenGL backend and is not implemented")
elseif(NOT SFML_HARMONY_DEVICE_FORM STREQUAL "MOBILE")
    message(FATAL_ERROR "Unsupported SFML_HARMONY_DEVICE_FORM: ${SFML_HARMONY_DEVICE_FORM}")
endif()

if(NOT DEFINED OHOS_SDK_NATIVE OR NOT EXISTS "${OHOS_SDK_NATIVE}/oh-uni-package.json")
    message(FATAL_ERROR "The OpenHarmony native SDK metadata could not be found; use the DevEco OHOS CMake toolchain")
endif()

file(READ "${OHOS_SDK_NATIVE}/oh-uni-package.json" _SFML_HARMONY_SDK_METADATA)
string(JSON _SFML_HARMONY_SDK_API ERROR_VARIABLE _SFML_HARMONY_SDK_API_ERROR
       GET "${_SFML_HARMONY_SDK_METADATA}" apiVersion)
if(_SFML_HARMONY_SDK_API_ERROR OR _SFML_HARMONY_SDK_API LESS _SFML_HARMONY_MIN_SDK_API)
    message(FATAL_ERROR "SFML requires an OpenHarmony native SDK with API level ${_SFML_HARMONY_MIN_SDK_API} or newer")
endif()

if(NOT DEFINED OHOS_STL OR NOT OHOS_STL STREQUAL "c++_shared")
    message(FATAL_ERROR "SFML's OpenHarmony backend requires the DevEco toolchain with OHOS_STL=c++_shared")
endif()

# OHOS_ARCH is selected and validated by the official toolchain. SFML only
# requires its value to keep independently installed ABI artifacts separate.
if(NOT DEFINED OHOS_ARCH OR OHOS_ARCH STREQUAL "")
    message(FATAL_ERROR "The DevEco OHOS CMake toolchain did not provide OHOS_ARCH")
endif()
if(NOT CMAKE_INSTALL_LIBDIR MATCHES "(^|/)${OHOS_ARCH}/?$")
    set(CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}/${OHOS_ARCH}")
endif()
# GNUInstallDirs computed the absolute companion before this module appended
# the ABI. Keep optional pkg-config and other consumers on the same ABI path.
GNUInstallDirs_get_absolute_install_dir(CMAKE_INSTALL_FULL_LIBDIR CMAKE_INSTALL_LIBDIR LIBDIR)

# Static SFML and bundled dependency archives are normally folded into one HAP
# NAPI shared object. Defining the default here, before any SFML or dependency
# targets are created, gives this module sole ownership of Harmony PIC policy.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

message(STATUS "SFML OpenHarmony mobile: minimum SDK API ${_SFML_HARMONY_MIN_SDK_API}, ABI ${OHOS_ARCH}, SDK API ${_SFML_HARMONY_SDK_API}")
