set(OPENH264_ROOT "${CMAKE_CURRENT_LIST_DIR}/vendor/openh264")
file(GLOB OPENH264_SOURCES
    "${OPENH264_ROOT}/codec/common/src/*.cpp"
    "${OPENH264_ROOT}/codec/decoder/core/src/*.cpp"
    "${OPENH264_ROOT}/codec/decoder/plus/src/*.cpp")
set(OPENH264_INCLUDES
    "${OPENH264_ROOT}/codec/api/wels"
    "${OPENH264_ROOT}/codec/common/inc"
    "${OPENH264_ROOT}/codec/decoder/core/inc"
    "${OPENH264_ROOT}/codec/decoder/plus/inc")
if(ESP_PLATFORM)
    idf_component_register(SRCS decoder.c ${OPENH264_SOURCES}
        INCLUDE_DIRS include PRIV_INCLUDE_DIRS ${OPENH264_INCLUDES}
        PRIV_REQUIRES pthread heap)
    set(OPENH264_TARGET ${COMPONENT_LIB})
else()
    cmake_minimum_required(VERSION 3.16)
    project(mosaico_openh264_host C CXX)
    add_library(openh264_decoder SHARED decoder.c ${OPENH264_SOURCES})
    target_include_directories(openh264_decoder PUBLIC include host PRIVATE ${OPENH264_INCLUDES})
    target_link_libraries(openh264_decoder PRIVATE pthread m)
    set(OPENH264_TARGET openh264_decoder)
endif()
# Portable implementation only. No unverified host SIMD/assembly on S31.
target_compile_features(${OPENH264_TARGET} PRIVATE cxx_std_11)
set_source_files_properties(${OPENH264_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;-fno-exceptions;-fno-rtti")
