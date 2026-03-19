set(API_HEADERS
    task_bluetooth.h
)

if(NOT CMAKE_ENABLE_RPI_MOD)
    set(SOURCES task_bluetooth.cpp)
endif()

target_sources(${projectTarget} PRIVATE ${API_HEADERS} ${SOURCES})

target_include_directories(${projectTarget} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
