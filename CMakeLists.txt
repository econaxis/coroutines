cmake_minimum_required(VERSION 3.10)
project(p1 C)

set(CMAKE_CXX_STANDARD 14)

add_link_options(-lm)
add_executable(p1
        a.o
        critical.c
        main.c)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
add_executable(gc1 gc-1.c)
target_include_directories(gc1 PRIVATE ${GLIB_INCLUDE_DIRS})
target_link_libraries(gc1 PRIVATE ${GLIB_LDFLAGS} m)

add_executable(gc gc.c)
target_include_directories(gc PRIVATE ${GLIB_INCLUDE_DIRS})
target_link_libraries(gc PRIVATE ${GLIB_LDFLAGS})
