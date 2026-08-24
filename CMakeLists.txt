cmake_minimum_required(VERSION 3.20)
project(AESBridge VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AESBRIDGE_BUILD_DRIVER "Build the libASPL AudioServerPlugIn" ON)
option(AESBRIDGE_BUILD_MANAGER "Build the SwiftUI manager" ON)
option(AESBRIDGE_BUILD_TESTS "Build automated tests" ON)
option(AESBRIDGE_BUILD_WINDOWS_BACKEND "Build the Windows network backend scaffold" ON)

function(aesbridge_strict_warnings target)
    if(MSVC)
        # C4324 is expected for the cache-line-aligned lock-free shared structs.
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /wd4324)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(AESBridgeProtocol STATIC
    Sources/Core/L24Codec.cpp
    Sources/Core/PTP.cpp
    Sources/Core/RTPPacket.cpp
    Sources/Core/SDP.cpp
    Sources/Core/SAP.cpp
    Sources/Core/SharedAudioRing.cpp
)
target_include_directories(AESBridgeProtocol PUBLIC Sources)
aesbridge_strict_warnings(AESBridgeProtocol)

if(WIN32)
    add_library(AESBridgeNetworking STATIC Sources/Platform/Windows/UDPSocketWindows.cpp)
    target_link_libraries(AESBridgeNetworking PUBLIC ws2_32)
else()
    add_library(AESBridgeNetworking STATIC Sources/Core/UDPSocket.cpp)
endif()
target_include_directories(AESBridgeNetworking PUBLIC Sources)
aesbridge_strict_warnings(AESBridgeNetworking)

if(UNIX)
add_library(AESBridgeCore STATIC
    Sources/Core/SessionDirectory.cpp
    Sources/Core/SharedAudioMemory.cpp
)
target_include_directories(AESBridgeCore PUBLIC Sources)
target_link_libraries(AESBridgeCore PUBLIC AESBridgeProtocol AESBridgeNetworking)
aesbridge_strict_warnings(AESBridgeCore)

add_executable(aes-bridge-engine
    Sources/Engine/EngineMain.cpp
    Sources/Engine/LiveEngine.cpp
    Sources/Engine/PTPClient.cpp
)
target_link_libraries(aes-bridge-engine PRIVATE AESBridgeCore)
aesbridge_strict_warnings(aes-bridge-engine)

if(APPLE AND AESBRIDGE_BUILD_DRIVER)
    find_path(ASPL_INCLUDE_DIR aspl/Plugin.hpp PATHS /opt/homebrew/include /usr/local/include)
    find_library(ASPL_LIBRARY ASPL PATHS /opt/homebrew/lib /usr/local/lib)
    if(NOT ASPL_INCLUDE_DIR OR NOT ASPL_LIBRARY)
        include(FetchContent)
        set(BUILD_TESTING OFF CACHE BOOL "Disable dependency tests" FORCE)
        FetchContent_Declare(libASPL
            GIT_REPOSITORY https://github.com/gavv/libASPL.git
            GIT_TAG 633e0f70203edd87d320fc5a3cae901e1363aac5
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(libASPL)
        set(ASPL_INCLUDE_DIR "${libaspl_SOURCE_DIR}/include")
        set(ASPL_LIBRARY libASPL)
    endif()
    if(ASPL_INCLUDE_DIR AND ASPL_LIBRARY)
        add_library(AESBridgeDriver MODULE
            Sources/Driver/PlugInMain.cpp
            Sources/Driver/LXToolDevice.cpp
            Sources/Driver/LXToolIOHandler.cpp
        )
        target_include_directories(AESBridgeDriver PRIVATE Sources "${ASPL_INCLUDE_DIR}")
        target_link_libraries(AESBridgeDriver PRIVATE AESBridgeCore "${ASPL_LIBRARY}" "-framework CoreAudio" "-framework CoreFoundation")
        set_target_properties(AESBridgeDriver PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION driver
            OUTPUT_NAME AESBridge
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Sources/Driver/Info.plist.in"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.maxpierr.aesbridge.driver"
            MACOSX_BUNDLE_BUNDLE_NAME "AES Bridge"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        )
        if(AESBRIDGE_BUILD_TESTS)
            add_executable(AESBridgeDriverSmoke Tests/DriverSmokeTest.cpp)
            target_include_directories(AESBridgeDriverSmoke PRIVATE Sources)
            target_link_libraries(AESBridgeDriverSmoke PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
            aesbridge_strict_warnings(AESBridgeDriverSmoke)
        endif()
    else()
        message(WARNING "libASPL not found: the protocol engine and tests will build, but not the Core Audio driver")
    endif()
endif()

if(APPLE AND AESBRIDGE_BUILD_MANAGER)
    add_custom_target(AESBridgeManager
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-manager.sh" "$<TARGET_FILE:aes-bridge-engine>"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building AES Bridge Manager"
    )
    add_dependencies(AESBridgeManager aes-bridge-engine)
endif()

endif() # UNIX

if(WIN32 AND AESBRIDGE_BUILD_WINDOWS_BACKEND)
    add_executable(aes-bridge-windows-backend
        Sources/Platform/Windows/WindowsBackendMain.cpp
        Sources/Platform/Windows/WindowsSharedAudioMemory.cpp
    )
    target_link_libraries(aes-bridge-windows-backend PRIVATE AESBridgeProtocol AESBridgeNetworking)
    aesbridge_strict_warnings(aes-bridge-windows-backend)
endif()

if(AESBRIDGE_BUILD_TESTS)
    enable_testing()
    if(TARGET AESBridgeDriverSmoke)
        add_test(NAME AESBridgeDriverHAL
            COMMAND AESBridgeDriverSmoke "$<TARGET_FILE:AESBridgeDriver>")
    endif()
    add_executable(AESBridgeProtocolTests Tests/ProtocolTests.cpp)
    target_link_libraries(AESBridgeProtocolTests PRIVATE AESBridgeProtocol)
    aesbridge_strict_warnings(AESBridgeProtocolTests)
    add_test(NAME AESBridgeProtocol COMMAND AESBridgeProtocolTests)
if(UNIX)
    add_executable(AESBridgeTests Tests/CoreTests.cpp)
    target_sources(AESBridgeTests PRIVATE Sources/Engine/LiveEngine.cpp Sources/Engine/PTPClient.cpp)
    target_link_libraries(AESBridgeTests PRIVATE AESBridgeCore)
    aesbridge_strict_warnings(AESBridgeTests)
    add_test(NAME AESBridgeCore COMMAND AESBridgeTests)
endif()
if(WIN32 AND AESBRIDGE_BUILD_WINDOWS_BACKEND)
    add_test(NAME AESBridgeWindowsBackend COMMAND aes-bridge-windows-backend --self-test)
endif()
endif()
