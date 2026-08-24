cmake_minimum_required(VERSION 3.20)
project(AESBridge VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AESBRIDGE_BUILD_DRIVER "Build the libASPL AudioServerPlugIn" ON)
option(AESBRIDGE_BUILD_MANAGER "Build the SwiftUI manager" ON)
option(AESBRIDGE_BUILD_TESTS "Build automated tests" ON)

add_library(AESBridgeCore STATIC
    Sources/Core/L24Codec.cpp
    Sources/Core/RTPPacket.cpp
    Sources/Core/SDP.cpp
    Sources/Core/SAP.cpp
    Sources/Core/UDPSocket.cpp
    Sources/Core/SharedAudioMemory.cpp
)
target_include_directories(AESBridgeCore PUBLIC Sources)
target_compile_options(AESBridgeCore PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(aes-bridge-engine
    Sources/Engine/EngineMain.cpp
    Sources/Engine/LiveEngine.cpp
)
target_link_libraries(aes-bridge-engine PRIVATE AESBridgeCore)
target_compile_options(aes-bridge-engine PRIVATE -Wall -Wextra -Wpedantic -Werror)

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

if(AESBRIDGE_BUILD_TESTS)
    enable_testing()
    add_executable(AESBridgeTests Tests/CoreTests.cpp)
    target_sources(AESBridgeTests PRIVATE Sources/Engine/LiveEngine.cpp)
    target_link_libraries(AESBridgeTests PRIVATE AESBridgeCore)
    target_compile_options(AESBridgeTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME AESBridgeCore COMMAND AESBridgeTests)
endif()
