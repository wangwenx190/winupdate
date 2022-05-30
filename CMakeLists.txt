#[[
  MIT License

  Copyright (C) 2022 by wangwenx190 (Yuhang Zhao)

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

cmake_minimum_required(VERSION 3.23)

project(WinUpdate
    VERSION 1.0.0.0
    DESCRIPTION "A convenient tool to update your Microsoft Store applications and Windows itself."
    HOMEPAGE_URL "https://github.com/wangwenx190/winupdate/"
    LANGUAGES CXX
)

if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

if(NOT DEFINED CMAKE_DEBUG_POSTFIX)
    if(WIN32)
        set(CMAKE_DEBUG_POSTFIX d)
    else()
        set(CMAKE_DEBUG_POSTFIX _debug)
    endif()
endif()

if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)
endif()

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

if(NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    app.manifest app.rc main.cpp
)

set(_WIN32_WINNT_WIN10 0x0A00)
set(NTDDI_WIN10_CO 0x0A00000B)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _CRT_NON_CONFORMING_SWPRINTFS _CRT_SECURE_NO_WARNINGS
    _ENABLE_EXTENDED_ALIGNED_STORAGE NOMINMAX UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN WINRT_LEAN_AND_MEAN WINVER=${_WIN32_WINNT_WIN10}
    _WIN32_WINNT=${_WIN32_WINNT_WIN10} _WIN32_IE=${_WIN32_WINNT_WIN10}
    NTDDI_VERSION=${NTDDI_WIN10_CO}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    /await:strict /bigobj /utf-8 /W4 /WX /guard:cf
    /guard:ehcont /Zc:__cplusplus /permissive- /GR
    /EHsc /d2FH4
)
target_link_options(${PROJECT_NAME} PRIVATE
    /DYNAMICBASE /HIGHENTROPYVA /LARGEADDRESSAWARE /NXCOMPAT
    /TSAWARE /GUARD:CF /guard:ehcont /CETCOMPAT /WX
)

if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    target_compile_options(${PROJECT_NAME} PRIVATE
        /Zc:inline /GA /Gy /Gw /GT
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        /OPT:REF /OPT:ICF
    )
endif()
