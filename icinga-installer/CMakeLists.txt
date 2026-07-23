# SPDX-FileCopyrightText: 2012 Icinga GmbH <https://icinga.com>
# SPDX-License-Identifier: GPL-2.0-or-later

foreach(flag_var
  CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
  CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
  if(${flag_var} MATCHES "/MD")
    string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
  endif(${flag_var} MATCHES "/MD")
endforeach(flag_var)

set(icinga_installer_SOURCES
  icinga-installer.cpp
)

add_executable(icinga-installer ${icinga_installer_SOURCES})

set_target_properties(
  icinga-installer PROPERTIES
  FOLDER Bin
  OUTPUT_NAME icinga2-installer
  LINK_FLAGS "/SUBSYSTEM:WINDOWS"

  # Use a statically-linked runtime library as this binary is run during the installation process where the other DLLs
  # may not have been installed already and the system-provided version may be too old.
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_link_libraries(icinga-installer shlwapi)

install(CODE "
  execute_process(COMMAND \${CMAKE_COMMAND} -E copy \"${CMAKE_CURRENT_BINARY_DIR}/icinga2.wixpatch.\${BUILD_TYPE}\"
    \"${CMAKE_CURRENT_BINARY_DIR}/icinga2.wixpatch\"
    RESULT_VARIABLE copy_result
    ERROR_VARIABLE error_output)
  if(copy_result)
    message(FATAL_ERROR \${error_output})
  endif()"
)

# Deterministic GUIDs for the MSI instance transforms: the UpgradeCode has to stay
# stable across versions (required for major upgrades) while the ProductCode has to
# change with every version (required by Windows Installer).
set(ICINGA2_GUID_NAMESPACE "52F2BEAA-4DF0-4C3E-ABDC-C0F61DE4DF8A")
foreach(instance RANGE 2 5)
  string(UUID ICINGA2_INSTANCE${instance}_PRODUCT_GUID
    NAMESPACE ${ICINGA2_GUID_NAMESPACE}
    NAME "icinga2-${ICINGA2_VERSION_SAFE}-instance${instance}-product"
    TYPE SHA1 UPPER)
  string(UUID ICINGA2_INSTANCE${instance}_UPGRADE_GUID
    NAMESPACE ${ICINGA2_GUID_NAMESPACE}
    NAME "icinga2-instance${instance}-upgrade"
    TYPE SHA1 UPPER)
endforeach()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/icinga2.wixpatch.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/icinga2.wixpatch.in"
  @ONLY
)

file(
  GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/icinga2.wixpatch.$<CONFIG>"
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/icinga2.wixpatch.in"
)

set(InstallPath "${CMAKE_INSTALL_SBINDIR}")

install(
  TARGETS icinga-installer
  RUNTIME DESTINATION ${InstallPath}
)
