cmake_minimum_required(VERSION 3.10)
project(dzo_anisotropic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED)

add_executable(dzo_anisotropic main.cpp)
target_link_libraries(dzo_anisotropic PRIVATE ${OpenCV_LIBS})
