# Check if lld is built as a standalone project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lld)
  cmake_minimum_required(VERSION 3.13.4)

  set(CMAKE_INCLUDE_CURRENT_DIR ON)
  set(LLD_BUILT_STANDALONE TRUE)

  find_program(LLVM_CONFIG_PATH "llvm-config" DOC "Path to llvm-config binary")
  if(NOT LLVM_CONFIG_PATH)
    message(FATAL_ERROR "llvm-config not found: specify LLVM_CONFIG_PATH")
  endif()

  execute_process(COMMAND "${LLVM_CONFIG_PATH}"
                          "--obj-root"
                          "--includedir"
                          "--cmakedir"
                          "--src-root"
                  RESULT_VARIABLE HAD_ERROR
                  OUTPUT_VARIABLE LLVM_CONFIG_OUTPUT
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(HAD_ERROR)
    message(FATAL_ERROR "llvm-config failed with status ${HAD_ERROR}")
  endif()

  string(REGEX REPLACE "[ \t]*[\r\n]+[ \t]*" ";" LLVM_CONFIG_OUTPUT "${LLVM_CONFIG_OUTPUT}")

  list(GET LLVM_CONFIG_OUTPUT 0 OBJ_ROOT)
  list(GET LLVM_CONFIG_OUTPUT 1 MAIN_INCLUDE_DIR)
  list(GET LLVM_CONFIG_OUTPUT 2 LLVM_CMAKE_DIR)
  list(GET LLVM_CONFIG_OUTPUT 3 MAIN_SRC_DIR)

  set(LLVM_OBJ_ROOT ${OBJ_ROOT} CACHE PATH "path to LLVM build tree")
  set(LLVM_MAIN_INCLUDE_DIR ${MAIN_INCLUDE_DIR} CACHE PATH "path to llvm/include")
  set(LLVM_MAIN_SRC_DIR ${MAIN_SRC_DIR} CACHE PATH "Path to LLVM source tree")

  file(TO_CMAKE_PATH ${LLVM_OBJ_ROOT} LLVM_BINARY_DIR)
  file(TO_CMAKE_PATH ${LLVM_CMAKE_DIR} LLVM_CMAKE_DIR)

  if(NOT EXISTS "${LLVM_CMAKE_DIR}/LLVMConfig.cmake")
    message(FATAL_ERROR "LLVMConfig.cmake not found")
  endif()
  include("${LLVM_CMAKE_DIR}/LLVMConfig.cmake")

  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")
  include_directories("${LLVM_BINARY_DIR}/include" ${LLVM_INCLUDE_DIRS})
  link_directories(${LLVM_LIBRARY_DIRS})

  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
  find_program(LLVM_TABLEGEN_EXE "llvm-tblgen" ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)

  include(AddLLVM)
  include(TableGen)
  include(HandleLLVMOptions)
  include(GetErrcMessages)
  include(CheckAtomic)

  if(LLVM_INCLUDE_TESTS)
    find_package(Python3 ${LLVM_MINIMUM_PYTHON_VERSION} REQUIRED
      COMPONENTS Interpreter)

    # Check prebuilt llvm/utils.
    if(EXISTS ${LLVM_TOOLS_BINARY_DIR}/FileCheck${CMAKE_EXECUTABLE_SUFFIX}
        AND EXISTS ${LLVM_TOOLS_BINARY_DIR}/not${CMAKE_EXECUTABLE_SUFFIX})
      set(LLVM_UTILS_PROVIDED ON)
    endif()

    if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      # Note: path not really used, except for checking if lit was found
      set(LLVM_LIT ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      if(NOT LLVM_UTILS_PROVIDED)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/FileCheck utils/FileCheck)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/not utils/not)
        set(LLVM_UTILS_PROVIDED ON)
        set(LLD_TEST_DEPS FileCheck not)
      endif()
      set(UNITTEST_DIR ${LLVM_MAIN_SRC_DIR}/utils/unittest)
      if(EXISTS ${UNITTEST_DIR}/googletest/include/gtest/gtest.h
          AND NOT EXISTS ${LLVM_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}
          AND EXISTS ${UNITTEST_DIR}/CMakeLists.txt)
        add_subdirectory(${UNITTEST_DIR} utils/unittest)
      endif()
    else()
      # Seek installed Lit.
      find_program(LLVM_LIT
                   NAMES llvm-lit lit.py lit
                   PATHS "${LLVM_MAIN_SRC_DIR}/utils/lit"
                   DOC "Path to lit.py")
    endif()

    if(LLVM_LIT)
      # Define the default arguments to use with 'lit', and an option for the user
      # to override.
      set(LIT_ARGS_DEFAULT "-sv")
      if (MSVC OR XCODE)
        set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
      endif()
      set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

      get_errc_messages(LLVM_LIT_ERRC_MESSAGES)

      # On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
      if(WIN32 AND NOT CYGWIN)
        set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
      endif()
    else()
      set(LLVM_INCLUDE_TESTS OFF)
    endif()
  endif()

  if(LLVM_HAVE_LIBXAR)
    set(XAR_LIB xar)
  endif()
endif()

set(LLD_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LLD_INCLUDE_DIR ${LLD_SOURCE_DIR}/include )
set(LLD_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(LLD_VENDOR ${PACKAGE_VENDOR} CACHE STRING
  "Vendor-specific text for showing with version information.")

if(LLD_VENDOR)
  add_definitions(-DLLD_VENDOR="${LLD_VENDOR}")
endif()

# Compute the LLD version from the LLVM version.
string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" LLD_VERSION
  ${PACKAGE_VERSION})
message(STATUS "LLD version: ${LLD_VERSION}")

string(REGEX REPLACE "([0-9]+)\\.[0-9]+(\\.[0-9]+)?" "\\1" LLD_VERSION_MAJOR
  ${LLD_VERSION})
string(REGEX REPLACE "[0-9]+\\.([0-9]+)(\\.[0-9]+)?" "\\1" LLD_VERSION_MINOR
  ${LLD_VERSION})

# Configure the Version.inc file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lld/Common/Version.inc.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/lld/Common/Version.inc)


if (CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. CMake would overwrite "
"the makefiles distributed with LLVM. Please create a directory and run cmake "
"from there, passing the path to this source directory as the last argument. "
"This process created the file `CMakeCache.txt' and the directory "
"`CMakeFiles'. Please delete them.")
endif()

list (APPEND CMAKE_MODULE_PATH "${LLD_SOURCE_DIR}/cmake/modules")

include(AddLLD)

option(LLD_USE_VTUNE
       "Enable VTune user task tracking."
       OFF)
if (LLD_USE_VTUNE)
  find_package(VTune)
  if (VTUNE_FOUND)
    include_directories(${VTune_INCLUDE_DIRS})
    list(APPEND LLVM_COMMON_LIBS ${VTune_LIBRARIES})
    add_definitions(-DLLD_HAS_VTUNE)
  endif()
endif()

option(LLD_BUILD_TOOLS
  "Build the lld tools. If OFF, just generate build targets." ON)

option(LLD_DEFAULT_LD_LLD_IS_MINGW
    "Use MinGW as the default backend for ld.lld. If OFF, ELF will be used." OFF)
if (LLD_DEFAULT_LD_LLD_IS_MINGW)
  add_definitions("-DLLD_DEFAULT_LD_LLD_IS_MINGW=1")
endif()

if (MSVC)
  add_definitions(-wd4530) # Suppress 'warning C4530: C++ exception handler used, but unwind semantics are not enabled.'
  add_definitions(-wd4062) # Suppress 'warning C4062: enumerator X in switch of enum Y is not handled' from system header.
endif()

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING
    PATTERN "*.h"
    )
endif()

add_subdirectory(Common)
add_subdirectory(tools/lld)

if (LLVM_INCLUDE_TESTS)
  add_subdirectory(test)
endif()

add_subdirectory(docs)
add_subdirectory(COFF)
add_subdirectory(ELF)
add_subdirectory(MachO)
add_subdirectory(MinGW)
add_subdirectory(wasm)

add_subdirectory(cmake/modules)
