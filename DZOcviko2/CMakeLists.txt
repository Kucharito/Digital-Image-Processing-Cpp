cmake_minimum_required(VERSION 3.10)
project(dzo_convolution VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

add_executable(dzo_convolution main.cpp)
target_link_libraries(dzo_convolution PRIVATE ${OpenCV_LIBS})
