# Icinga 2
# Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

cmake_minimum_required(VERSION 2.6)
set(BOOST_MIN_VERSION "1.41.0")

project(icinga2)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif()

file(STRINGS icinga2.spec VERSION_LINE REGEX "^Version: ")
string(REPLACE "Version: " "" ICINGA2_VERSION ${VERSION_LINE})

set(ICINGA2_USER "icinga" CACHE STRING "Icinga 2 user")
set(ICINGA2_GROUP "icinga" CACHE STRING "Icinga 2 group")
set(ICINGA2_COMMAND_USER "icinga" CACHE STRING "Icinga 2 command user")
set(ICINGA2_COMMAND_GROUP "icingacmd" CACHE STRING "Icinga 2 command group")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/COPYING" ICINGA2_LICENSE_GPL)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/COPYING.Exceptions" ICINGA2_LICENSE_ADDITIONS)
set(ICINGA2_LICENSE "${ICINGA2_LICENSE_GPL}\n\n---\n\n${ICINGA2_LICENSE_ADDITIONS}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" ${ICINGA2_LICENSE})

include(GetGitRevisionDescription)
git_describe(GIT_VERSION --tags)
if(GIT_VERSION MATCHES "-NOTFOUND$")
  configure_file(icinga-version.h.fallback ${CMAKE_CURRENT_BINARY_DIR}/icinga-version.h COPYONLY)
else()
  configure_file(icinga-version.h.cmake icinga-version.h)
  configure_file(${CMAKE_CURRENT_BINARY_DIR}/icinga-version.h ${CMAKE_CURRENT_SOURCE_DIR}/icinga-version.h.fallback COPYONLY)
endif()

if(WIN32)
  set(Boost_USE_STATIC_LIBS ON)
  add_definitions(-DBOOST_ALL_NO_LIB)
endif()

find_package(Boost ${BOOST_MIN_VERSION} COMPONENTS thread system program_options regex REQUIRED)

link_directories(${Boost_LIBRARY_DIRS})
include_directories(${Boost_INCLUDE_DIRS})

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/lib ${CMAKE_CURRENT_SOURCE_DIR}/components
  ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lib ${CMAKE_CURRENT_BINARY_DIR}/components
)

#set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

if(APPLE)
  set(CMAKE_INSTALL_NAME_DIR "@executable_path/../lib/icinga2")
endif(APPLE)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Qunused-arguments -g")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments -g")
endif()

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  if(${CMAKE_SYSTEM_NAME} MATCHES AIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -lpthread")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
  endif()
endif()

if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
endif()

set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/Bin/${CMAKE_BUILD_TYPE} CACHE PATH "Library output path")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/Bin/${CMAKE_BUILD_TYPE} CACHE PATH "Executable output path")

include(CheckFunctionExists)
include(CheckLibraryExists)
check_function_exists(vfork HAVE_VFORK)
check_function_exists(backtrace_symbols HAVE_BACKTRACE_SYMBOLS)
check_function_exists(pipe2 HAVE_PIPE2)
check_library_exists(dl dladdr "dlfcn.h" HAVE_DLADDR)
check_library_exists(crypto BIO_f_zlib "" HAVE_BIOZLIB)
check_library_exists(execinfo backtrace_symbols "" HAVE_LIBEXECINFO)

if(HAVE_LIBEXECINFO)
  set(HAVE_BACKTRACE_SYMBOLS TRUE)
endif()

include(GNUInstallDirs)
configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h ESCAPE_QUOTES)

install(
  FILES README COPYING COPYING.Exceptions AUTHORS ChangeLog INSTALL NEWS
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

include(CTest)
enable_testing()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(third-party)
add_subdirectory(tools)
add_subdirectory(lib)
add_subdirectory(components)
add_subdirectory(icinga-app)
add_subdirectory(etc)
add_subdirectory(itl)
add_subdirectory(doc)
add_subdirectory(test)
add_subdirectory(pki)

set(CPACK_PACKAGE_NAME "Icinga2")
set(CPACK_PACKAGE_VENDOR "Icinga Development Team")
set(CPACK_PACKAGE_VERSION ${ICINGA2_VERSION})
set(CPACK_NSIS_DISPLAY_NAME "Icinga 2")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ICINGA2")
set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icinga-app\\\\icinga.ico")
set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icinga-app\\\\icinga.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/icinga-app\\\\icinga.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "sbin\\\\icinga2.exe")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
set(CPACK_SOURCE_IGNORE_FILES "/.git/" "/debian/" "/.vagrant/" "/release/" "/debug/" "/build/" )

set(CPACK_NSIS_EXECUTABLES_DIRECTORY "sbin")
set(CPACK_PACKAGE_EXECUTABLES "Icinga2SetupAgent;Icinga 2 Agent Wizard")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Icinga2SetupAgent")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "nsExec::Exec '\\\"$INSTDIR\\\\sbin\\\\icinga2\\\" --scm-uninstall'")

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)

if(WIN32)
  install(
    PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
      C:\\\\openssl\\\\bin\\\\libeay32.dll C:\\\\openssl\\\\bin\\\\ssleay32.dll
    DESTINATION ${CMAKE_INSTALL_SBINDIR}
  )
endif()

include(CPack)