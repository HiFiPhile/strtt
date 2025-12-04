include(CMakePrintHelpers)
include(FetchContent)
cmake_print_variables(CMAKE_VERSION)

# pthread
set(THREADS_PREFER_PTHREAD_FLAG)
include(FindThreads)
cmake_print_variables(CMAKE_USE_PTHREADS_INIT)

# sources
set(strtt_source_files
    strtt.cpp
    strttapp.cpp)

add_executable(strtt ${strtt_source_files})

IF (WIN32)
    target_link_libraries(strtt stlink ws2_32)
ELSE()
    target_link_libraries(strtt stlink)
ENDIF()


add_custom_command(
    TARGET strtt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:strtt> ${CMAKE_CURRENT_SOURCE_DIR}/../../
)
