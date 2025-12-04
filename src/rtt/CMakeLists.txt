# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# C sources
set(strtt_source_files
    strtt.c
    inputparser.c
    strttapp.c)

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
