cmake_minimum_required(VERSION 3.15)

project(qping
    VERSION 1.1.0
    DESCRIPTION "Windows Ping 替代工具，支持高级扫描功能"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "qping 仅支持 Windows 平台")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QPING_ENABLE_WARNINGS "启用编译器警告" ON)

set(QPING_SOURCES
    src/main.cpp
    src/ping.cpp
    src/target.cpp
)

set(QPING_HEADERS
    src/qping.h
)

add_executable(qping ${QPING_SOURCES} ${QPING_HEADERS})

target_include_directories(qping PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(qping PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    if(QPING_ENABLE_WARNINGS)
        target_compile_options(qping PRIVATE /W4 /permissive-)
    endif()
    target_compile_definitions(qping PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    if(QPING_ENABLE_WARNINGS)
        target_compile_options(qping PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

target_link_libraries(qping PRIVATE Iphlpapi Ws2_32)

include(GNUInstallDirs)
install(TARGETS qping RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(CPACK_PACKAGE_NAME "qping")
set(CPACK_PACKAGE_VENDOR "mrchzh")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Windows Ping 替代工具")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
include(CPack)

message(STATUS "项目: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
