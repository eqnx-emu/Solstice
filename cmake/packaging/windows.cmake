# windows specific packaging
install(TARGETS sunshine RUNTIME DESTINATION "." COMPONENT application)

# Hardening: include zlib1.dll (loaded via LoadLibrary() in openssl's libcrypto.a)
install(FILES "${ZLIB}" DESTINATION "." COMPONENT application)

# ARM64: include minhook-detours DLL (shared library for ARM64)
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64" AND DEFINED _MINHOOK_DLL)
    install(FILES "${_MINHOOK_DLL}" DESTINATION "." COMPONENT application)
endif()

# Optional: bundle libVIIPER.dll so gamepad_driver = viiper works from the installer
set(SUNSHINE_VIIPER_DLL "" CACHE FILEPATH "Path to libVIIPER.dll to bundle with the Windows package")
if(SUNSHINE_VIIPER_DLL)
    if(NOT EXISTS "${SUNSHINE_VIIPER_DLL}")
        message(FATAL_ERROR "SUNSHINE_VIIPER_DLL does not exist: ${SUNSHINE_VIIPER_DLL}")
    endif()
    install(FILES "${SUNSHINE_VIIPER_DLL}" DESTINATION "." COMPONENT application)
    get_filename_component(_viiper_dir "${SUNSHINE_VIIPER_DLL}" DIRECTORY)
    if(EXISTS "${_viiper_dir}/licenses.txt")
        install(FILES "${_viiper_dir}/licenses.txt" DESTINATION "." RENAME "libVIIPER-licenses.txt"
                COMPONENT application)
    endif()
endif()

# Bundle the SudoVDA virtual display driver (OFF by default, the fork workflow turns it ON). install.ps1 runs from sunshine-setup.ps1 (NSIS and WiX).
option(SUNSHINE_BUNDLE_SUDOVDA "Bundle the SudoVDA virtual display driver (needs SudoVDA.dll and nefconc.exe)" OFF)
set(SUNSHINE_SUDOVDA_DIR "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/drivers/sudovda")
if(SUNSHINE_BUNDLE_SUDOVDA)
    foreach(_sudovda_file SudoVDA.dll nefconc.exe)
        if(NOT EXISTS "${SUNSHINE_SUDOVDA_DIR}/${_sudovda_file}")
            message(FATAL_ERROR
                    "SUNSHINE_BUNDLE_SUDOVDA is ON but ${_sudovda_file} is missing from ${SUNSHINE_SUDOVDA_DIR}. "
                    "Download it (see .github/workflows/fork-windows.yml) or configure with "
                    "-DSUNSHINE_BUNDLE_SUDOVDA=OFF.")
        endif()
    endforeach()
    install(DIRECTORY "${SUNSHINE_SUDOVDA_DIR}/"
            DESTINATION "drivers/sudovda"
            COMPONENT application
            FILES_MATCHING
            PATTERN "SudoVDA.dll"
            PATTERN "SudoVDA.inf"
            PATTERN "sudovda.cat"
            PATTERN "sudovda.cer"
            PATTERN "nefconc.exe"
            PATTERN "install.ps1"
            PATTERN "uninstall.ps1"
            PATTERN "LICENSE")
endif()

# Adding tools
install(TARGETS dxgi-info RUNTIME DESTINATION "tools" COMPONENT dxgi)
install(TARGETS audio-info RUNTIME DESTINATION "tools" COMPONENT audio)

# Mandatory tools
install(TARGETS sunshinesvc RUNTIME DESTINATION "tools" COMPONENT application)
install(TARGETS solstice-netstress RUNTIME DESTINATION "tools" COMPONENT application)

# Mandatory scripts
install(FILES
        "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/sunshine-setup.ps1"
        "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/sunshine-setup-process.ps1"
        DESTINATION "scripts"
        COMPONENT assets)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/service/"
        DESTINATION "scripts"
        COMPONENT assets)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/migration/"
        DESTINATION "scripts"
        COMPONENT assets)
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/path/"
        DESTINATION "scripts"
        COMPONENT assets)

# Configurable options for the service
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/autostart/"
        DESTINATION "scripts"
        COMPONENT autostart)

# scripts
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/misc/firewall/"
        DESTINATION "scripts"
        COMPONENT firewall)

# Sunshine assets
install(DIRECTORY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
        DESTINATION "${SUNSHINE_ASSETS_DIR}"
        COMPONENT assets)

# copy assets (excluding shaders) to build directory, for running without install
file(COPY "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/"
        DESTINATION "${CMAKE_BINARY_DIR}/assets"
        PATTERN "shaders" EXCLUDE)
# use junction for shaders directory
cmake_path(CONVERT "${SUNSHINE_SOURCE_ASSETS_DIR}/windows/assets/shaders"
        TO_NATIVE_PATH_LIST shaders_in_build_src_native)
cmake_path(CONVERT "${CMAKE_BINARY_DIR}/assets/shaders" TO_NATIVE_PATH_LIST shaders_in_build_dest_native)
execute_process(COMMAND cmd.exe /c mklink /J "${shaders_in_build_dest_native}" "${shaders_in_build_src_native}")

set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}\\\\sunshine.ico")

# The name of the directory that will be created in C:/Program files/
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")

# Setting components groups and dependencies
set(CPACK_COMPONENT_GROUP_CORE_EXPANDED true)

# sunshine binary
set(CPACK_COMPONENT_APPLICATION_DISPLAY_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_COMPONENT_APPLICATION_DESCRIPTION "${CMAKE_PROJECT_NAME} main application and required components.")
set(CPACK_COMPONENT_APPLICATION_GROUP "Core")
set(CPACK_COMPONENT_APPLICATION_REQUIRED true)
set(CPACK_COMPONENT_APPLICATION_DEPENDS assets)

# service auto-start script
set(CPACK_COMPONENT_AUTOSTART_DISPLAY_NAME "Launch on Startup")
set(CPACK_COMPONENT_AUTOSTART_DESCRIPTION "If enabled, launches Sunshine automatically on system startup.")
set(CPACK_COMPONENT_AUTOSTART_GROUP "Core")

# assets
set(CPACK_COMPONENT_ASSETS_DISPLAY_NAME "Required Assets")
set(CPACK_COMPONENT_ASSETS_DESCRIPTION "Shaders, default box art, and web UI.")
set(CPACK_COMPONENT_ASSETS_GROUP "Core")
set(CPACK_COMPONENT_ASSETS_REQUIRED true)

# audio tool
set(CPACK_COMPONENT_AUDIO_DISPLAY_NAME "audio-info")
set(CPACK_COMPONENT_AUDIO_DESCRIPTION "CLI tool providing information about sound devices.")
set(CPACK_COMPONENT_AUDIO_GROUP "Tools")

# display tool
set(CPACK_COMPONENT_DXGI_DISPLAY_NAME "dxgi-info")
set(CPACK_COMPONENT_DXGI_DESCRIPTION "CLI tool providing information about graphics cards and displays.")
set(CPACK_COMPONENT_DXGI_GROUP "Tools")

# firewall scripts
set(CPACK_COMPONENT_FIREWALL_DISPLAY_NAME "Add Firewall Exclusions")
set(CPACK_COMPONENT_FIREWALL_DESCRIPTION "Scripts to enable or disable firewall rules.")
set(CPACK_COMPONENT_FIREWALL_GROUP "Scripts")

# include specific packaging
include(${CMAKE_MODULE_PATH}/packaging/windows_nsis.cmake)
include(${CMAKE_MODULE_PATH}/packaging/windows_wix.cmake)
