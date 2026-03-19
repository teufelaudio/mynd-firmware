set(API_HEADERS
    battery.h
)

set(SOURCES
    battery.cpp
)

target_sources(${projectTarget} PRIVATE ${API_HEADERS} ${SOURCES})

target_include_directories(${projectTarget} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_subdirectory(battery_indicator)
add_subdirectory(charge_controller)
add_subdirectory(history)
add_subdirectory(soc_estimator)
add_subdirectory(temperature)
