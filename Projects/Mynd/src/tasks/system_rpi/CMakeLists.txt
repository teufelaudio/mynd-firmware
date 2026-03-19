set(API_HEADERS
    task_system.h
)

set(SOURCES
    task_system.cpp
)

target_sources(${projectTarget} PRIVATE ${API_HEADERS} ${SOURCES})

target_include_directories(${projectTarget} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
