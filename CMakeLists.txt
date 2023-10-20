#[[
  MIT License

  Copyright (C) 2023 by wangwenx190 (Yuhang Zhao)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
]]

cmake_minimum_required(VERSION 3.28)

project(WinUpdate
    VERSION "1.0.0.0"
    DESCRIPTION "A convenient tool to update Microsoft Store applications and Windows."
    HOMEPAGE_URL "https://wangwenx190.github.io/winupdate/"
)

option(WU_ENABLE_VCLTL "Enable VC-LTL." OFF)
option(WU_ENABLE_YYTHUNKS "Enable YY-Thunks." OFF)

include(cmake/utils.cmake)

setup_project(
    ENABLE_LTO
    MAX_WARNING
    #WARNINGS_ARE_ERRORS
    RTTI
    EXCEPTIONS
    LANGUAGES CXX
)

if(MSVC)
    if(WU_ENABLE_VCLTL)
        include(cmake/VC-LTL.cmake)
        if("x${SupportLTL}" STREQUAL "xtrue")
            # Make sure we will always overwrite the previous settings.
            unset(CMAKE_MSVC_RUNTIME_LIBRARY)
            unset(CMAKE_MSVC_RUNTIME_LIBRARY CACHE)
            #unset(CMAKE_MSVC_RUNTIME_LIBRARY PARENT_SCOPE)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
        endif()
    endif()
    if(WU_ENABLE_YYTHUNKS)
        unset(YYTHUNKS_TARGET_OS)
        unset(YYTHUNKS_TARGET_OS CACHE)
        #unset(YYTHUNKS_TARGET_OS PARENT_SCOPE)
        set(YYTHUNKS_TARGET_OS "WinXP" CACHE STRING "" FORCE)
        include(cmake/YY-Thunks.cmake)
    endif()
endif()

add_executable(${PROJECT_NAME})

set(__rc_path "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.rc")
generate_win32_rc_file(
    PATH "${__rc_path}"
    VERSION "${PROJECT_VERSION}"
    COMPANY "wangwenx190"
    DESCRIPTION "${PROJECT_DESCRIPTION}"
    COPYRIGHT "MIT License"
    PRODUCT "Windows Updater"
    ICONS "app.ico"
)

set(__manifest_path "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.manifest")
generate_win32_manifest_file(
    PATH "${__manifest_path}"
    ID "org.wangwenx190.winupdate"
    VERSION "${PROJECT_VERSION}"
    DESCRIPTION "${PROJECT_DESCRIPTION}"
    VISTA_COMPAT
    WIN7_COMPAT
    WIN8_COMPAT
    WIN8_1_COMPAT
    WIN10_COMPAT
    WIN11_COMPAT
    XAML_ISLANDS_COMPAT
    UTF8_CODEPAGE
)

target_sources(${PROJECT_NAME} PRIVATE
    "${__rc_path}"
    "${__manifest_path}"
    main.cpp
)

set(SYSCMDLINE_BUILD_STATIC ON)
set(SYSCMDLINE_BUILD_TESTS OFF)
set(SYSCMDLINE_INSTALL OFF)
add_subdirectory(syscmdline)

setup_compile_params(
    TARGETS ${PROJECT_NAME} syscmdline
    SPECTRE
    EHCONTGUARD
    PERMISSIVE
    INTELCET
    INTELJCC
    CFGUARD
    FORCE_LTO
    SECURE_CODE
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    syscmdline
)

install2(TARGETS ${PROJECT_NAME})
