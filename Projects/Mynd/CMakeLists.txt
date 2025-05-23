CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

PROJECT(mynd ASM C CXX)

include(stm32_gcc)

find_package(CMSIS COMPONENTS STM32F0 REQUIRED QUIET)
find_package(HAL COMPONENTS RCC CORTEX ADC GPIO UART TIM I2C FLASH DMA LL_USART PWR RTC REQUIRED QUIET)

set(TeufelLibsPath "${CMAKE_CURRENT_SOURCE_DIR}/external/teufel/libs")
list(APPEND CMAKE_MODULE_PATH "${TeufelLibsPath}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/teufel/drivers")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/thirdparty")
find_package(TeufelDrivers COMPONENTS aw9523b bq25713 button STM32_vEEPROM tas5805m tas5825p tps25751 REQUIRED QUIET)
find_package(TeufelLibraries COMPONENTS app_assert greeting syscalls REQUIRED QUIET)

find_package(FreeRTOS COMPONENTS ARM_CM0 REQUIRED QUIET)
find_package(Actionslink REQUIRED QUIET)
find_package(Logger REQUIRED QUIET)
find_package(IEngine REQUIRED QUIET)

set(MYND_HEAP_SIZE 824)
set(MYND_STACK_SIZE 512)

set(MYND_COMPILER_FLAGS
    LOGGER_USE_EXTERNAL_THREAD=1

    # BOARD_CONFIG_HAS_NO_I2C_MODE
)

set(PROD_TEST_COMPILER_FLAGS
    # tshell's printf configuration
    PRINTF_DISABLE_SUPPORT_EXPONENTIAL=1
    PRINTF_DISABLE_SUPPORT_FLOAT=1
    PRINTF_DISABLE_SUPPORT_LONG_LONG=1
    BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE=1

    # TSHELL_HELP_DISABLED=1

    INCLUDE_PRODUCTION_TESTS
)

set(ALL_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TeufelDrivers_INCLUDE_DIR}
    ${TeufelLibraries_INCLUDE_DIR}

    src
)

SET(PROJECT_SOURCES
    # Platform drivers
    ${CMAKE_SOURCE_DIR}/drivers/platform/stm32/i2c_freertos.c

    src/stm32f0xx_it.c
    src/main.cpp
)

SET(ALL_SOURCES
    ${CMAKE_SOURCE_DIR}/sdk/STM32CubeF0/Drivers/CMSIS/Device/ST/STM32F0xx/Source/Templates/gcc/startup_stm32f072xb.s
    ${TeufelDrivers_SOURCES}
    ${TeufelLibraries_SOURCES}
    ${TEUFEL_SOURCES}
    ${PROJECT_SOURCES}
)

file(GLOB PROTO_COMMON_FILES ${TeufelLibsPath}/actionslink/proto/common/*.proto)
file(GLOB PROTO_FILES ${TeufelLibsPath}/actionslink/proto/eco/*.proto)

# Nanopb
set(NANOPB_SRC_ROOT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/external/thirdparty/nanopb)
set(NANOPB_IMPORT_DIRS
    ${TeufelLibsPath}/actionslink/proto/common
    ${TeufelLibsPath}/actionslink/proto/eco
)
find_package(Nanopb REQUIRED)

# Common proto files
NANOPB_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_COMMON_FILES})

# Project specific files
NANOPB_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

configure_file (
    "${TeufelLibsPath}/actionslink/src/version/actionslink_version.h.in"
    "${CMAKE_BINARY_DIR}/Projects/Mynd/actionslink_version.h"
)

# Base _interface_ target. This is used to propagate the compiler flags and include directories to all targets
add_library(baseTarget INTERFACE IMPORTED)

# Enable LTO for both compilation and linking
# -flto=auto prevents warnings about performing LTO with parallel threads
target_compile_options(baseTarget INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-std=c++20 -flto=auto -DNDEBUG>)
target_compile_options(baseTarget INTERFACE $<$<COMPILE_LANGUAGE:C>:-std=gnu99 -flto=auto -DNDEBUG>)
target_compile_options(baseTarget INTERFACE $<$<COMPILE_LANGUAGE:ASM>:-flto=auto -DNDEBUG>)
target_link_options(baseTarget INTERFACE -flto=auto -DNDEBUG)
#target_link_options(baseTarget INTERFACE -flto=auto -DNDEBUG -u _printf_float)

target_link_libraries(baseTarget INTERFACE
    CMSIS::STM32::F072RB
    STM32::NoSys
    HAL::STM32::F0::RCC
    HAL::STM32::F0::RCCEx
    HAL::STM32::F0::CORTEX
    HAL::STM32::F0::GPIO
    HAL::STM32::F0::TIMEx
    HAL::STM32::F0::UART
    HAL::STM32::F0::UARTEx
    HAL::STM32::F0::LL_USART
    HAL::STM32::F0::PWR
    HAL::STM32::F0::I2CEx
    HAL::STM32::F0::FLASH
    HAL::STM32::F0::FLASHEx
    HAL::STM32::F0::DMA
    HAL::STM32::F0::ADC
    HAL::STM32::F0::ADCEx
)

target_link_libraries(baseTarget INTERFACE
    FreeRTOS::Timers
    FreeRTOS::ARM_CM0
    FreeRTOS::StreamBuffer
)

target_link_libraries(baseTarget INTERFACE
    IEngine::Pattern::Generic
)

target_include_directories(baseTarget INTERFACE
    ${ALL_INCLUDES}
    ${NANOPB_INCLUDE_DIRS}
    ${CMAKE_BINARY_DIR}/Projects/Mynd     # .proto files generated headers go to bin folder
)

target_compile_definitions(baseTarget INTERFACE ${MYND_COMPILER_FLAGS})

################################################
############# Application target ###############
################################################
set(projectTarget ${PROJECT_NAME})
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

target_link_libraries(${projectTarget} PRIVATE
    Actionslink
    Actionslink::LogLevelInfo
)

target_link_libraries(${projectTarget} PRIVATE
    Logger
    Logger::Config2
    Logger::Format1
)

teufel_cmsis_generate_default_linker_script(${projectTarget} F0 F072RB ${MYND_HEAP_SIZE} ${MYND_STACK_SIZE} 0x08000000 128K-4K 0x20000000 0x4000)

GITVERSION_ENABLE(${projectTarget})
ADD_HEX_TARGET(${projectTarget})
PRINT_SIZE_OF_TARGETS(${projectTarget})

add_deploy_jlink(${projectTarget})

################################################
###### Application target (FACTORY) #######
################################################
set(projectTarget ${PROJECT_NAME}-factory)
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

target_compile_definitions(${projectTarget} PRIVATE ${PROD_TEST_COMPILER_FLAGS})

target_link_libraries(${projectTarget} PRIVATE
    Actionslink
    Actionslink::LogLevelInfo
)

target_link_libraries(${projectTarget} PRIVATE
    Logger
    Logger::Config2
    Logger::Format2
)

teufel_cmsis_generate_default_linker_script(${projectTarget} F0 F072RB ${MYND_HEAP_SIZE} ${MYND_STACK_SIZE} 0x08000000 128K-4K 0x20000000 0x4000)

GITVERSION_ENABLE(${projectTarget})
ADD_HEX_TARGET(${projectTarget})
PRINT_SIZE_OF_TARGETS(${projectTarget})

add_deploy_jlink(${projectTarget})

################################################
########## Update target - MYND ##########
################################################
set(update-fw "${PROJECT_NAME}-update-firmware-mcu")
set(fw-offset "${PROJECT_NAME}-offset")

set(projectTarget ${fw-offset})
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

target_link_libraries(${projectTarget} PRIVATE
    Actionslink
    Actionslink::LogLevelInfo
)

target_link_libraries(${projectTarget} PRIVATE
    Logger
    Logger::Config2
    Logger::Format1
)

target_compile_definitions(${projectTarget} PRIVATE BOOTLOADER)

teufel_cmsis_generate_default_linker_script(${projectTarget} F0 F072RB ${MYND_HEAP_SIZE} ${MYND_STACK_SIZE} 0x08005000 128K-20K-4K 0x200000BC 0x3f44)

GITVERSION_ENABLE(${projectTarget})
ADD_ELF_HEX_BIN_TARGETS(${projectTarget})
PRINT_SIZE_OF_TARGETS(${projectTarget})

################################################
######## Update target (FACTORY) ##########
################################################

set(update-factory-fw "${PROJECT_NAME}-factory-update-firmware-mcu")
set(fw-factory-offset "${PROJECT_NAME}-factory-offset")

set(projectTarget ${fw-factory-offset})
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

target_link_libraries(${projectTarget} PRIVATE
    Actionslink
    Actionslink::LogLevelOff
)

target_link_libraries(${projectTarget} PRIVATE
    Logger
    Logger::Config2
    Logger::Format2
)

target_compile_definitions(${projectTarget} PRIVATE BOOTLOADER ${PROD_TEST_COMPILER_FLAGS})

teufel_cmsis_generate_default_linker_script(${projectTarget} F0 F072RB ${MYND_HEAP_SIZE} ${MYND_STACK_SIZE} 0x08005000 128K-20K-4K 0x200000BC 0x3f44)

GITVERSION_ENABLE(${projectTarget})
ADD_ELF_HEX_BIN_TARGETS(${projectTarget})
PRINT_SIZE_OF_TARGETS(${projectTarget})

add_custom_command(OUTPUT mynd-update-firmware-mcu.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/support/scripts/prepare_update.py
    -p OA2302
    -k ${CMAKE_SOURCE_DIR}/support/keys/teufel_dev_private.pem
    --no-encryption
    --mcu=${PROJECT_BINARY_DIR}/mynd-offset.bin
    -o mynd-update-firmware-mcu.bin
    COMMENT "Prepare update file"
    VERBATIM
)

add_custom_command(OUTPUT mynd-factory-update-firmware-mcu.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/support/scripts/prepare_update.py
    -p OA2302
    -k ${CMAKE_SOURCE_DIR}/support/keys/teufel_dev_private.pem
    --no-encryption
    --mcu=${PROJECT_BINARY_DIR}/mynd-factory-offset.bin
    -o mynd-factory-update-firmware-mcu.bin
    COMMENT "Prepare factory update file"
    VERBATIM
)

add_custom_target(mynd-update-firmware-mcu DEPENDS mynd-offset.bin mynd-update-firmware-mcu.bin)
add_custom_target(mynd-factory-update-firmware-mcu DEPENDS mynd-factory-offset.bin mynd-factory-update-firmware-mcu.bin)

################################################
######## Complete target - MYND ##########
################################################
set(BOOTLOADER_VERSION "v2.1.0")
set(BOOTLOADER_FILENAME "${PROJECT_NAME}-bootloader-${BOOTLOADER_VERSION}.hex")

replace_dashes_with_underscores(${PROJECT_NAME} _PROJECT_NAME)

add_external_binary_dependency(
    ${BOOTLOADER_FILENAME}
    https://repo.bag.software/de.teufel.mcu.firmware-releases/de/teufel/firmware/${PROJECT_NAME}/bootloader
)

add_custom_command(OUTPUT ${PROJECT_NAME}-complete.hex
    # TODO: add Windows support
    COMMAND hexmerge.py
    ${CMAKE_SOURCE_DIR}/dl/${BOOTLOADER_FILENAME}:0x08000000:0x08005000
#    ${CMAKE_BINARY_DIR}/Projects/MyndBootloader/${PROJECT_NAME}-bootloader.hex:0x08000000:0x08006800
    ${CMAKE_BINARY_DIR}/Projects/Mynd/${PROJECT_NAME}-offset.hex:0x08005000:0x08020000
    -o ${PROJECT_NAME}-complete.hex
    DEPENDS ${PROJECT_NAME}-offset.hex ${BOOTLOADER_FILENAME}_dl-from-artifactory
    COMMENT "Merge hex files: ${PROJECT_NAME}-bootloader.hex and ${PROJECT_NAME}-offset.hex into ${PROJECT_NAME}-complete.hex"
    VERBATIM
)

add_custom_command(OUTPUT ${PROJECT_NAME}-factory-complete.hex
    # TODO: add Windows support
    COMMAND hexmerge.py
    ${CMAKE_SOURCE_DIR}/dl/${BOOTLOADER_FILENAME}:0x08000000:0x08005000
#    ${CMAKE_BINARY_DIR}/Projects/MyndBootloader/${PROJECT_NAME}-bootloader.hex:0x08000000:0x08006800
    ${CMAKE_BINARY_DIR}/Projects/Mynd/${PROJECT_NAME}-factory-offset.hex:0x08005000:0x08020000
    -o ${PROJECT_NAME}-factory-complete.hex
    DEPENDS ${PROJECT_NAME}-factory-offset.hex ${BOOTLOADER_FILENAME}_dl-from-artifactory
    COMMENT "Merge hex files: ${PROJECT_NAME}-bootloader.hex and ${PROJECT_NAME}-factory-offset.hex into ${PROJECT_NAME}-factory-complete.hex"
    VERBATIM
)

add_custom_target(${PROJECT_NAME}-complete DEPENDS ${PROJECT_NAME}-complete.hex)
add_custom_target(${PROJECT_NAME}-factory-complete DEPENDS ${PROJECT_NAME}-factory-complete.hex)
