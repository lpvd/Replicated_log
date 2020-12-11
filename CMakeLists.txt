cmake_minimum_required( VERSION 3.14.0 )

project(replico)

find_package(Boost)
include_directories(${Boost_INCLUDE_DIRS})

if(Boost_FOUND)
    MESSAGE( STATUS "Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}.")
    MESSAGE( STATUS "Boost_LIBRARIES = ${BOOST_LIBRARYDIR}.")
    MESSAGE( STATUS "Boost_LIB_VERSION = ${Boost_LIB_VERSION}.")
endif()
    
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
find_package(Boost COMPONENTS system date_time REQUIRED)


add_subdirectory(googletest)

# server 
add_library(replico STATIC 
    replico/replico_server.cpp 
    replico/replico_server.h)

target_link_libraries(replico ${Boost_SYSTEM_LIBRARY} ${Boost_DATE_TIME_LIBRARY})
target_include_directories(replico PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/replico>)

#tests
add_executable(replico_tests test_main.cpp)
target_link_libraries(replico_tests replico gtest gtest_main)