
# Add all the headers to the project for IDEs.
file(GLOB_RECURSE LIBCXX_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../include/*)
if(WIN32)
    file(GLOB LIBCXX_WIN32_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../include/__support/win32/*.h)
    list(APPEND LIBCXX_HEADERS ${LIBCXX_WIN32_HEADERS})
endif()
# Force them all into the headers dir on MSVC, otherwise they end up at
# project scope because they don't have extensions.
if (MSVC_IDE)
source_group("Header Files" FILES ${LIBCXX_HEADERS})
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../include/__config_site.in" "${CMAKE_CURRENT_SOURCE_DIR}/__config_site" @ONLY)

add_custom_command(OUTPUT std.pcm
    COMMAND clang++ -std=c++2b --precompile ${CMAKE_CURRENT_SOURCE_DIR}/std.cppm
                -o ${PROJECT_BINARY_DIR}/std.pcm
                -nostdinc++
                -nostdlib++
                -D_LIBCPP_BUILDING_LIBRARY
                -isystem ${CMAKE_CURRENT_SOURCE_DIR}/
                -isystem ${CMAKE_CURRENT_SOURCE_DIR}/../include
                -isystem ${CMAKE_CURRENT_SOURCE_DIR}/../../libcxxabi/../libcxx/src
                -isystem ${CMAKE_CURRENT_SOURCE_DIR}/../../libcxxabi/include
    DEPENDS ${LIBCXX_HEADERS}
)

add_custom_target(std_module ALL DEPENDS std.pcm)

add_library(std_module_lib DEPENDS std_module)

target_sources()
