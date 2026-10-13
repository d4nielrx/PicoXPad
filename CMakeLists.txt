cmake_minimum_required(VERSION 3.10.2)
project(picoxpad_daemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Android-specific settings
if(ANDROID)
    set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION "clang")
    set(CMAKE_SYSTEM_NAME "Android")
    set(CMAKE_SYSTEM_VERSION 29)  # API level 29 (Android 10)
endif()

add_executable(picoxpad_daemon picoxpad_daemon.cpp)

target_compile_options(picoxpad_daemon PRIVATE
    -Wall -Wextra -O2 -D_GNU_SOURCE
)

target_link_libraries(picoxpad_daemon PRIVATE
    log
    pthread
    android
)

# Install to module directory
install(TARGETS picoxpad_daemon
    RUNTIME DESTINATION .
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
)