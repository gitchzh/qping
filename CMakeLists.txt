cmake_minimum_required(VERSION 3.15)

project(qping
    VERSION 1.2.0
    DESCRIPTION "Windows Ping 替代工具，支持高级扫描功能"
    LANGUAGES CXX
)

# 设置 Windows 目标版本为 Windows Vista (0x0600)，兼容 Windows 7/8/10/11
# 0x0600 = Windows Vista / Server 2008
add_definitions(-D_WIN32_WINNT=0x0600)

# MSVC 静态运行时库设置（必须在 project() 之后）
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "qping 仅支持 Windows 平台")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

set(CMAKE_CXX_STANDARD 14)
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
    # MinGW/GCC: 完全静态链接，避免依赖 libgcc_s_dw2-1.dll、libstdc++-6.dll、libwinpthread-1.dll 等
    target_link_options(qping PRIVATE -static -static-libgcc -static-libstdc++)
    # 设置字符集：如果源代码是 UTF-8，使用 utf-8；如果是 GBK，使用 gbk
    # 执行字符集统一使用 GBK，匹配中文 Windows 系统默认代码页 (936)
    # 注意：如果源代码是 GBK 编码，将 -finput-charset=utf-8 改为 -finput-charset=gbk
    target_compile_options(qping PRIVATE -finput-charset=utf-8 -fexec-charset=gbk)
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
message(STATUS "Windows 目标版本: 0x0600 (Vista/Server 2008)")
if(MSVC)
    message(STATUS "MSVC 运行时库: 静态链接")
else()
    message(STATUS "MinGW 链接: 完全静态链接")
endif()
