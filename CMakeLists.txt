cmake_minimum_required(VERSION 3.20)

project ("NegativeScanner")
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    set(CMAKE_BUILD_TYPE_RELEASE)

    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(
    NegativeScanner
    ${SOURCES}
)

find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)
find_package(GLEW REQUIRED)

target_include_directories(
    NegativeScanner PUBLIC
    ${OPENGL_INCLUDE_DIRS}
    ${GLUT_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    "include/"
)

target_link_libraries(
    NegativeScanner
    ${OPENGL_LIBRARIES}
    ${GLUT_LIBRARIES}
    ${GLEW_LIBRARIES}
)

set_property(
    TARGET NegativeScanner
    PROPERTY CXX_STANDARD 17
)