CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

PROJECT(mynd-bootloader ASM C)

include(stm32_gcc)

find_package(CMSIS COMPONENTS STM32F0 REQUIRED QUIET)
find_package(HAL COMPONENTS RCC CORTEX GPIO UART I2C FLASH DMA LL_USART LL_USB PCD CRC PWR REQUIRED QUIET)
find_package(USBDevice COMPONENTS CDC MSC REQUIRED QUIET)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/teufel/libs")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/teufel/drivers")
set(TeufelLibsPath "${CMAKE_CURRENT_SOURCE_DIR}/external/teufel/libs")
find_package(TeufelDrivers COMPONENTS aw9523b REQUIRED QUIET)
find_package(TeufelLibraries COMPONENTS app_assert syscalls REQUIRED QUIET)

find_package(Tboot)
find_package(Logger)

set(ALL_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${USBDevice_INCLUDE_DIR}
    ${TeufelDrivers_INCLUDE_DIR}

    bsp
    bsp/debug_uart
    bsp/shared_i2c

    board/io_expander
    board/power_supply
    board/usb_switch

    ${CMAKE_SOURCE_DIR}/drivers

    ${CMAKE_SOURCE_DIR}/middleware/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_SOURCE_DIR}/middleware/ST/STM32_USB_Device_Library/Class/MSC/Inc
)

set(PROJECT_SOURCES
    # Platform drivers
    ${CMAKE_SOURCE_DIR}/drivers/platform/stm32/i2c.c

    ${TeufelDrivers_SOURCES}
    ${TeufelLibraries_SOURCES}

    board/io_expander/board_link_io_expander.c
    board/io_expander/board_link_io_expander.h
    board/power_supply/board_link_power_supply.c
    board/power_supply/board_link_power_supply.h
    board/usb_switch/board_link_usb_switch.c
    board/usb_switch/board_link_usb_switch.h

    main.c
    stm32f0xx_it.c

    dfu_mcu.c
    dfu_mcu.h

    bsp/board.c
    bsp/board.h
    bsp/board_msp.c
    bsp/debug_uart/bsp_debug_uart.c
    bsp/debug_uart/bsp_debug_uart.h
    bsp/shared_i2c/bsp_shared_i2c.c
    bsp/shared_i2c/bsp_shared_i2c.h
)

set(ALL_SOURCES
    ${CMAKE_SOURCE_DIR}/sdk/STM32CubeF0/Drivers/CMSIS/Device/ST/STM32F0xx/Source/Templates/gcc/startup_stm32f072xb.s
    ${USBDevice_SOURCES}
    ${PROJECT_SOURCES}
    ${T_BOOT_SOURCES}
)

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# Enable LTO for both compilation and linking
target_compile_options(${PROJECT_NAME} PRIVATE -flto=auto -DNDEBUG)
target_link_options(${PROJECT_NAME} PRIVATE -flto=auto -DNDEBUG)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CMSIS::STM32::F072RB
    STM32::NoSys
    HAL::STM32::F0::RCC
    HAL::STM32::F0::RCCEx
    HAL::STM32::F0::CORTEX
    HAL::STM32::F0::CRC
    HAL::STM32::F0::PCD
    HAL::STM32::F0::PCDEx
    HAL::STM32::F0::GPIO
    HAL::STM32::F0::UART
    HAL::STM32::F0::UARTEx
    HAL::STM32::F0::LL_USART
    HAL::STM32::F0::PWR
    HAL::STM32::F0::I2CEx
    HAL::STM32::F0::FLASH
    HAL::STM32::F0::FLASHEx
    HAL::STM32::F0::DMA
    HAL::STM32::F0::LL_USB
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Logger
    Logger::ConfigOff
    Logger::Format1
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Tboot::RamDisk
    Tboot::Platform::Stm32
    Tboot::Platform::Stm32::Usb
    Tboot::Platform::Stm32::Crc
)

target_include_directories(${PROJECT_NAME} PRIVATE ${ALL_INCLUDES})

teufel_cmsis_generate_default_linker_script(${PROJECT_NAME} F0 F072RB 0x100 0x400 0x08000000 20K 0x20000100 12K)

GITVERSION_ENABLE(${PROJECT_NAME})

ADD_ELF_HEX_BIN_TARGETS(${PROJECT_NAME})
PRINT_SIZE_OF_TARGETS(${PROJECT_NAME})
