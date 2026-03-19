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

set(MYND_RPI_MOD_COMPILER_FLAGS
    MYND_RPI_MODIFICATION=1

    LOGGER_USE_EXTERNAL_THREAD=1

    # Enables dual volume control of amp gain and Moode MPD gain.
    HYBRID_VOLUME_MODE=1

    # Enable at your own discretion. 
    # Disabled by default because when the daemon changes the bluez volume via the volume
    # buttons, the stored bluez volume becomes out of sync with the what the phone/device thinks
    # the volume is. Then, when changed on the bt device, it leads to large jumps in volume
    # when it corrects.
    # This is only relevant when HYBRID_VOLUME_MODE is enabled.
    # ENABLE_BT_RENDERER_VOLUME_CONTROL=1

    # Enable tshell's configure_wifi command to allow setting a new or known WiFi network on RPi
    # using the MYND's CLI via USB-C port.
    # Be aware: the optional password argument is visible in the local process list while the
    # command is being entered/executed and must be provided when configuring a new network.
    ENABLE_CONFIGURE_WIFI_COMMAND=1

    # Enable hotspot request via BT button triple press to bring up an
    # Access Point (AP) hotspot.
    # Connect to this hotspot for instant access to Moode webUI and/or RPi via SSH.
    # The daemon decides whether this activates a config-defined hotspot or the
    # existing Moode hotspot (set password via webUI to enable), based on
    # mynd-firmware/Projects/Mynd/src/tasks/rpi/daemon_install/payload/config/mynd_rpi_link.conf
    ENABLE_RPI_LINK_HOTSPOT=1

    # Enable internal FTDI chip for USB debugging via USB-C
    # port using a typical USB-C data cable.
    # Requires the MYNDberry custom adapter pcb rpi hat to be installed
    # and the uart debug pinout to be connected to the pcb 
    # adapter's uart debug pins.
    ENABLE_INTERNAL_FTDI=1
)

set(FACTORY_TEST_COMPILER_FLAGS
    # tshell's printf configuration
    PRINTF_DISABLE_SUPPORT_EXPONENTIAL=1
    PRINTF_DISABLE_SUPPORT_FLOAT=1
    PRINTF_DISABLE_SUPPORT_LONG_LONG=1
    BOARD_CONFIG_BATTERY_LEVEL_ESTIMATOR_SIMPLE=1

    # TSHELL_HELP_DISABLED=1

    INCLUDE_FACTORY_TESTS
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
file(GLOB PROTO_ECO_FILES ${TeufelLibsPath}/actionslink/proto/eco/*.proto)
file(GLOB PROTO_RPI_FILES ${TeufelLibsPath}/actionslink/proto/rpi/*.proto)

# Nanopb
set(NANOPB_SRC_ROOT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/external/thirdparty/nanopb)
set(NANOPB_IMPORT_DIRS
    ${TeufelLibsPath}/actionslink/proto/common
    ${TeufelLibsPath}/actionslink/proto/eco
    ${TeufelLibsPath}/actionslink/proto/rpi
)
find_package(Nanopb REQUIRED)

# Common proto files
NANOPB_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_COMMON_FILES})

# Generate eco and rpi proto files
# Use RELPATH to put them in separate directories to avoid conflicts (both have message.proto)
# RELPATH must be set to the parent directory so eco/ and rpi/ subdirectories are preserved
NANOPB_GENERATE_CPP(PROTO_ECO_SRCS PROTO_ECO_HDRS RELPATH ${TeufelLibsPath}/actionslink/proto ${PROTO_ECO_FILES})
NANOPB_GENERATE_CPP(PROTO_RPI_SRCS PROTO_RPI_HDRS RELPATH ${TeufelLibsPath}/actionslink/proto ${PROTO_RPI_FILES})

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
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_ECO_SRCS} ${PROTO_ECO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

# Add eco subdirectory to include path so message.pb.h can be found
target_include_directories(${projectTarget} PRIVATE ${CMAKE_BINARY_DIR}/Projects/Mynd/eco)

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
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_ECO_SRCS} ${PROTO_ECO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

# Add eco subdirectory to include path so message.pb.h can be found
target_include_directories(${projectTarget} PRIVATE ${CMAKE_BINARY_DIR}/Projects/Mynd/eco)

target_compile_definitions(${projectTarget} PRIVATE ${FACTORY_TEST_COMPILER_FLAGS})

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
###### Application target (RPI MOD) #######
################################################
set(projectTarget ${PROJECT_NAME}berry)
set(CMAKE_ENABLE_RPI_MOD ON)
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_RPI_SRCS} ${PROTO_RPI_HDRS})
add_subdirectory(src ${projectTarget}_build)
set(CMAKE_ENABLE_RPI_MOD OFF)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

# Add rpi subdirectory to include path so message.pb.h can be found
target_include_directories(${projectTarget} PRIVATE ${CMAKE_BINARY_DIR}/Projects/Mynd/rpi)

target_compile_definitions(${projectTarget} PRIVATE ${MYND_RPI_MOD_COMPILER_FLAGS})

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
########## Update target - MYND ##########
################################################
set(update-fw "${PROJECT_NAME}-update-firmware-mcu")
set(fw-offset "${PROJECT_NAME}-offset")

set(projectTarget ${fw-offset})
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_ECO_SRCS} ${PROTO_ECO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

# Add eco subdirectory to include path so message.pb.h can be found
target_include_directories(${projectTarget} PRIVATE ${CMAKE_BINARY_DIR}/Projects/Mynd/eco)

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
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_ECO_SRCS} ${PROTO_ECO_HDRS})
add_subdirectory(src ${projectTarget}_build)
target_link_libraries(${projectTarget} PRIVATE baseTarget)

# Add eco subdirectory to include path so message.pb.h can be found
target_include_directories(${projectTarget} PRIVATE ${CMAKE_BINARY_DIR}/Projects/Mynd/eco)

target_link_libraries(${projectTarget} PRIVATE
    Actionslink
    Actionslink::LogLevelOff
)

target_link_libraries(${projectTarget} PRIVATE
    Logger
    Logger::Config2
    Logger::Format2
)

target_compile_definitions(${projectTarget} PRIVATE BOOTLOADER ${FACTORY_TEST_COMPILER_FLAGS})

teufel_cmsis_generate_default_linker_script(${projectTarget} F0 F072RB ${MYND_HEAP_SIZE} ${MYND_STACK_SIZE} 0x08005000 128K-20K-4K 0x200000BC 0x3f44)

GITVERSION_ENABLE(${projectTarget})
ADD_ELF_HEX_BIN_TARGETS(${projectTarget})
PRINT_SIZE_OF_TARGETS(${projectTarget})

################################################
########## Update target - RPi Mod ##########
################################################
set(update-myndberry-fw "${PROJECT_NAME}berry-update-firmware-mcu")
set(fw-myndberry-offset "${PROJECT_NAME}berry-offset")

set(projectTarget ${fw-myndberry-offset})
set(CMAKE_ENABLE_RPI_MOD ON)
add_executable(${projectTarget} ${ALL_SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_RPI_SRCS} ${PROTO_RPI_HDRS})
add_subdirectory(src ${projectTarget}_build)
set(CMAKE_ENABLE_RPI_MOD OFF)

target_link_libraries(${projectTarget} PRIVATE baseTarget)

# Add rpi subdirectory to include path so message.pb.h can be found
target_include_directories(${projectTarget} PRIVATE ${CMAKE_BINARY_DIR}/Projects/Mynd/rpi)

target_link_libraries(${projectTarget} PRIVATE
    Actionslink
    Actionslink::LogLevelInfo
)

target_link_libraries(${projectTarget} PRIVATE
    Logger
    Logger::Config2
    Logger::Format1
)

target_compile_definitions(${projectTarget} PRIVATE BOOTLOADER ${MYND_RPI_MOD_COMPILER_FLAGS})

teufel_cmsis_generate_default_linker_script(${projectTarget} F0 F072RB ${MYND_HEAP_SIZE} ${MYND_STACK_SIZE} 0x08005000 128K-20K-4K 0x200000BC 0x3f44)

GITVERSION_ENABLE(${projectTarget})
ADD_ELF_HEX_BIN_TARGETS(${projectTarget})
PRINT_SIZE_OF_TARGETS(${projectTarget})

add_custom_command(OUTPUT ${update-fw}.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/support/scripts/prepare_update.py
    -p OA2302
    -k ${CMAKE_SOURCE_DIR}/support/keys/teufel_dev_private.pem
    --no-encryption
    --mcu=${PROJECT_BINARY_DIR}/${fw-offset}.bin
    -o ${update-fw}.bin
    COMMENT "Prepare update file"
    VERBATIM
)

add_custom_command(OUTPUT ${update-factory-fw}.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/support/scripts/prepare_update.py
    -p OA2302
    -k ${CMAKE_SOURCE_DIR}/support/keys/teufel_dev_private.pem
    --no-encryption
    --mcu=${PROJECT_BINARY_DIR}/${fw-factory-offset}.bin
    -o ${update-factory-fw}.bin
    COMMENT "Prepare factory update file"
    VERBATIM
)
    
add_custom_command(OUTPUT ${update-myndberry-fw}.bin
    COMMAND python3 ${CMAKE_SOURCE_DIR}/support/scripts/prepare_update.py
    -p OA2302
    -k ${CMAKE_SOURCE_DIR}/support/keys/teufel_dev_private.pem
    --no-encryption
    --mcu=${PROJECT_BINARY_DIR}/${fw-myndberry-offset}.bin
    -o ${update-myndberry-fw}.bin
    COMMENT "Prepare rpi mod update file"
    VERBATIM
)

add_custom_target(${update-fw} DEPENDS ${fw-offset}.bin ${update-fw}.bin)
add_custom_target(${update-factory-fw} DEPENDS ${fw-factory-offset}.bin ${update-factory-fw}.bin)
add_custom_target(${update-myndberry-fw} DEPENDS ${fw-myndberry-offset}.bin ${update-myndberry-fw}.bin)

################################################
######## Complete target - MYND ##########
################################################
# Skip adding MyndBootloader in CI - it's already added by root CMakeLists.txt
if(NOT DEFINED ENV{CI})
    add_subdirectory(${CMAKE_SOURCE_DIR}/Projects/MyndBootloader ${CMAKE_BINARY_DIR}/Projects/MyndBootloader EXCLUDE_FROM_ALL)
endif()

set(BOOTLOADER_FILENAME "${PROJECT_NAME}-bootloader.hex")
set(BOOTLOADER_TARGET "mynd-bootloader.hex")

replace_dashes_with_underscores(${PROJECT_NAME} _PROJECT_NAME)

add_custom_command(OUTPUT ${PROJECT_NAME}-complete.hex
    COMMAND hexmerge.py
    ${CMAKE_SOURCE_DIR}/build/Projects/MyndBootloader/${BOOTLOADER_FILENAME}:0x08000000:0x08005000
    ${CMAKE_BINARY_DIR}/Projects/Mynd/${PROJECT_NAME}-offset.hex:0x08005000:0x08020000
    -o ${PROJECT_NAME}-complete.hex
    DEPENDS ${PROJECT_NAME}-offset.hex ${BOOTLOADER_TARGET}
    COMMENT "Merge hex files: ${PROJECT_NAME}-bootloader.hex and ${PROJECT_NAME}-offset.hex into ${PROJECT_NAME}-complete.hex"
    VERBATIM
)

add_custom_command(OUTPUT ${PROJECT_NAME}-factory-complete.hex
    COMMAND hexmerge.py
    ${CMAKE_SOURCE_DIR}/build/Projects/MyndBootloader/${BOOTLOADER_FILENAME}:0x08000000:0x08005000
    ${CMAKE_BINARY_DIR}/Projects/Mynd/${PROJECT_NAME}-factory-offset.hex:0x08005000:0x08020000
    -o ${PROJECT_NAME}-factory-complete.hex
    DEPENDS ${PROJECT_NAME}-factory-offset.hex ${BOOTLOADER_TARGET}
    COMMENT "Merge hex files: ${PROJECT_NAME}-bootloader.hex and ${PROJECT_NAME}-factory-offset.hex into ${PROJECT_NAME}-factory-complete.hex"
    VERBATIM
)

add_custom_command(OUTPUT ${PROJECT_NAME}berry-complete.hex
    COMMAND hexmerge.py
    ${CMAKE_SOURCE_DIR}/build/Projects/MyndBootloader/${BOOTLOADER_FILENAME}:0x08000000:0x08005000
    ${CMAKE_BINARY_DIR}/Projects/Mynd/${PROJECT_NAME}berry-offset.hex:0x08005000:0x08020000
    -o ${PROJECT_NAME}berry-complete.hex
    DEPENDS ${PROJECT_NAME}berry-offset.hex ${BOOTLOADER_TARGET}
    COMMENT "Merge hex files: ${PROJECT_NAME}-bootloader.hex and ${PROJECT_NAME}berry-offset.hex into ${PROJECT_NAME}berry-complete-firmware-mcu.hex"
    VERBATIM
)

add_custom_target(${PROJECT_NAME}-complete DEPENDS ${PROJECT_NAME}-complete.hex)
add_custom_target(${PROJECT_NAME}-factory-complete DEPENDS ${PROJECT_NAME}-factory-complete.hex)
add_custom_target(${PROJECT_NAME}berry-complete DEPENDS ${PROJECT_NAME}berry-complete.hex)
