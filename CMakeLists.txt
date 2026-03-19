cmake_minimum_required (VERSION 3.13)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/support/cmake)

include(utils)
include(deploy)
include(format)

find_package(Python3)
message(STATUS "Found python executable at: " ${Python3_EXECUTABLE})
# for required python packages, refer to requirements.txt
# and run pip install -r requirements.txt

installGitHooks()

set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_ASM_FLAGS_RELEASE "")

set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_ASM_FLAGS_DEBUG "")

if(NOT TOOLCHAIN_PREFIX)
     set(TOOLCHAIN_PREFIX "/usr")
     message(STATUS "No TOOLCHAIN_PREFIX specified, using default: " ${TOOLCHAIN_PREFIX})
else()
     file(TO_CMAKE_PATH "${TOOLCHAIN_PREFIX}" TOOLCHAIN_PREFIX)
endif()

if(NOT TARGET_TRIPLET)
    set(TARGET_TRIPLET "arm-none-eabi")
    message(STATUS "No TARGET_TRIPLET specified, using default: " ${TARGET_TRIPLET})
endif()

set(TOOLCHAIN_BIN_DIR "${TOOLCHAIN_PREFIX}/bin")
set(TOOLCHAIN_INC_DIR "${TOOLCHAIN_PREFIX}/${TARGET_TRIPLET}/include")
set(TOOLCHAIN_LIB_DIR "${TOOLCHAIN_PREFIX}/${TARGET_TRIPLET}/lib")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(WIN32)
    SET(TOOL_EXECUTABLE_SUFFIX ".exe")
else()
    SET(TOOL_EXECUTABLE_SUFFIX "")
endif()

if(${CMAKE_VERSION} VERSION_LESS 3.6.0)
    include(CMakeForceCompiler)
    CMAKE_FORCE_C_COMPILER("${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc${TOOL_EXECUTABLE_SUFFIX}" GNU)
    CMAKE_FORCE_CXX_COMPILER("${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-g++${TOOL_EXECUTABLE_SUFFIX}" GNU)
else()
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc${TOOL_EXECUTABLE_SUFFIX}")
    set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-g++${TOOL_EXECUTABLE_SUFFIX}")
endif()
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc${TOOL_EXECUTABLE_SUFFIX}")

set(CMAKE_OBJCOPY "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-objcopy${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "objcopy tool")
set(CMAKE_OBJDUMP "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-objdump${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "objdump tool")
set(CMAKE_SIZE "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-size${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "size tool")
set(CMAKE_DEBUGER "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gdb${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "debuger")
set(CMAKE_CPPFILT "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-c++filt${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "C++filt")
set(CMAKE_AR "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc-ar${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "gcc-ar")
set(CMAKE_RANLIB "${TOOLCHAIN_BIN_DIR}/${TARGET_TRIPLET}-gcc-ranlib${TOOL_EXECUTABLE_SUFFIX}" CACHE INTERNAL "ranlib")

# This global function enables _pre-build_ build-time command, which executes
# every make call to run git describe, get current version and update gitversion.h.
# To enable this option gitversion.h MUST be added as dependency into ADD_EXECUTABLE function.
function(GITVERSION_ENABLE TARGET)
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    add_custom_command(
        OUTPUT gitversion.h
        COMMAND ${CMAKE_COMMAND}
        -Droot_dir="${CMAKE_SOURCE_DIR}"
        -Dbin_dir="${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_SOURCE_DIR}/support/cmake/gitversion.cmake"
    )

    # gitversion.h file MUST be set as dependency for the TARGET
    # to enable versioning by "git describe"
    add_custom_target(GITVERSION_${TARGET} DEPENDS "gitversion.h")
    add_dependencies(${TARGET} GITVERSION_${TARGET})

if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/gitversion.h)
    # Set target's parameter VERSION, which can be used from the Project's cmake.
    file(READ "${CMAKE_CURRENT_BINARY_DIR}/gitversion.h" gitversionfile)
    string(REGEX MATCH "#define VERSION_MAJOR ([0-9]*)" VERS_MAJ ${gitversionfile})
    if(VERS_MAJ)
        set(V_MAJ ${CMAKE_MATCH_1})
    else()
        set(V_MAJ 0)
    endif()
    string(REGEX MATCH "#define VERSION_MINOR ([0-9]*)" VERS_MIN ${gitversionfile})
    if(VERS_MIN)
        set(V_MIN ${CMAKE_MATCH_1})
    else()
        set(V_MIN 0)
    endif()
    string(REGEX MATCH "#define VERSION_PATCH ([0-9]*)" VERS_PATCH ${gitversionfile})
    if(VERS_PATCH)
        set(V_PATCH ${CMAKE_MATCH_1})
    else()
        set(V_PATCH 0)
    endif()
    string(REGEX MATCH "#define VERSION_BUILD ([a-zA-Z0-9-]+)" VERS_BUILD ${gitversionfile})
    if(VERS_BUILD)
        set(VERS_BUILD ${CMAKE_MATCH_1})
    else()
        set(VERS_BUILD "")
    endif()

    if (VERS_BUILD)
        set_target_properties(${TARGET} PROPERTIES VERSION "v${V_MAJ}.${V_MIN}.${V_PATCH}-${VERS_BUILD}")
    else()
        set_target_properties(${TARGET} PROPERTIES VERSION "v${V_MAJ}.${V_MIN}.${V_PATCH}")
    endif()
endif()
endfunction()

function(MAKE_FULL_IMAGE_TARGETS TARGET BOOTLOADER OUTPUT)
    IF(EXECUTABLE_OUTPUT_PATH)
      SET(FILENAME "${EXECUTABLE_OUTPUT_PATH}/${TARGET}")
    ELSE()
      SET(FILENAME "${TARGET}")
    ENDIF()
    ADD_CUSTOM_TARGET(${OUTPUT}.bin COMMAND python ${CMAKE_SOURCE_DIR}/support/scripts/make_flash.py --bootloader=${BOOTLOADER} --app=${TARGET}.bin -o ${OUTPUT}.bin)
endfunction()

function(check_compiler_version compiler_id compiler_version)
    if (NOT ${CMAKE_C_COMPILER_ID} STREQUAL ${compiler_id})
        message(FATAL_ERROR "Current compiler ${CMAKE_C_COMPILER_ID} is not supported. Please use GCC!")
    endif()

    if (NOT ${CMAKE_C_COMPILER_VERSION} STREQUAL "${compiler_version}")
        message(FATAL_ERROR "Provided compiler version v${CMAKE_CXX_COMPILER_VERSION} is not supported. Please use GCC v${compiler_version}!")
    endif()
endfunction()

include_directories(${CMAKE_SOURCE_DIR})

project(mynd)
set(TeufelProjects
        Mynd
        MyndBootloader
)

check_compiler_version("GNU" "13.2.1")

if(NOT PROJECT)
    foreach(prj IN LISTS TeufelProjects)
        message("\n ▣ ${prj} configuration...")
        add_subdirectory(Projects/${prj})
    endforeach()
else()
    if (${PROJECT} IN_LIST TeufelProjects)
        message("\n ▣ ${PROJECT} configuration...")
        add_subdirectory(Projects/${PROJECT})
    else()
        message(FATAL_ERROR "Unknown project: ${PROJECT}")
    endif()
endif()
