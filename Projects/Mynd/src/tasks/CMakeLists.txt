set(API_HEADERS
    task_priorities.h
)

target_sources(${projectTarget} PRIVATE ${API_HEADERS})

target_include_directories(${projectTarget} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

if (CMAKE_ENABLE_RPI_MOD)
    add_subdirectory(audio_rpi)
    add_subdirectory(rpi)
    add_subdirectory(system_rpi)
else()
    add_subdirectory(audio)
    add_subdirectory(system)
    add_subdirectory(bluetooth)
endif()
