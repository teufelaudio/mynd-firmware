set(API_HEADERS
    task_audio.h
)

set(SOURCES
    task_audio.cpp
)

target_sources(${projectTarget} PRIVATE ${API_HEADERS} ${SOURCES})

target_include_directories(${projectTarget} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
