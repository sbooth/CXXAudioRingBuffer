cmake_minimum_required(VERSION 3.15)
project(CXXAudioRingBuffer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(CXXAudioRingBuffer
    Sources/CXXAudioRingBuffer/spsc/AudioRingBuffer.cpp
    Sources/CXXAudioRingBuffer/include/spsc/AudioRingBuffer.hpp
)
add_library(spsc::AudioRingBuffer ALIAS CXXAudioRingBuffer)

target_include_directories(CXXAudioRingBuffer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Sources/CXXAudioRingBuffer/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(CXXAudioRingBuffer PRIVATE /W4)
else()
    target_compile_options(CXXAudioRingBuffer PRIVATE -Wall -Wextra)
     if(APPLE)
         target_link_libraries(CXXAudioRingBuffer PRIVATE "-framework CoreAudio")
     endif()
endif()

include(GNUInstallDirs)

install(TARGETS CXXAudioRingBuffer
    EXPORT CXXAudioRingBufferTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY Sources/CXXAudioRingBuffer/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT CXXAudioRingBufferTargets
    FILE CXXAudioRingBufferTargets.cmake
    NAMESPACE spsc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CXXAudioRingBuffer
)

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    enable_testing()

    add_executable(run_tests test/audio_ring_buffer_test.cpp)

    target_link_libraries(run_tests
        PRIVATE
            spsc::AudioRingBuffer
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(run_tests)
endif()
