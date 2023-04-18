cmake_minimum_required(VERSION 2.8)

project(file_io_hook)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Werror -Wextra -fPIC -Wno-error=unused-parameter -fno-omit-frame-pointer")

set(CMAKE_CXX_STANDARD 11)

include_directories(
    src
)

file(GLOB DEFAULT_HOOK_SRC
    src/default_hook_io_handle.cpp
)

file(GLOB IO_HOOK_SRC
    src/hook_io_handle.cpp
    src/io_hook.cpp
)

file(GLOB EXAMPLE_SRC
    examples/example.cpp 
)

add_library(default_hook SHARED ${DEFAULT_HOOK_SRC})
add_library(io_hook SHARED ${IO_HOOK_SRC})
add_executable(example ${EXAMPLE_SRC})

target_link_libraries(io_hook
    pthread
    dl
)

target_link_libraries(example
    default_hook
)

set(CMAKE_INSTALL_PREFIX "./file_io_hook")
# set(CMAKE_INSTALL_LIBDIR "./file_io_hook")
set(INSTALL_DIR "./")
install(TARGETS default_hook
    LIBRARY DESTINATION ${INSTALL_DIR}/lib
)
install(TARGETS io_hook
    LIBRARY DESTINATION ${INSTALL_DIR}/lib
)
install(TARGETS example
    RUNTIME DESTINATION ${INSTALL_DIR}/bin
)

file(GLOB_RECURSE HEADERS
    src/*.h
)
install(FILES ${HEADERS} DESTINATION ${INSTALL_DIR}/include)