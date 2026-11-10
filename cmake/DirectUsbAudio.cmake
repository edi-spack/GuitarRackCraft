# The pinned submodule is shared with the standalone driver project.
set(DIRECTUSB_ROOT "${GRC_ROOT}/3rd_party/directusb")
if(NOT EXISTS "${DIRECTUSB_ROOT}/oboe-directusb/CMakeLists.txt")
    message(FATAL_ERROR "Run git submodule update --init 3rd_party/directusb")
endif()
set(OBOE_ROOT "${DIRECTUSB_ROOT}/oboe-directusb/third_party/oboe")
add_subdirectory("${DIRECTUSB_ROOT}/oboe-directusb" "${CMAKE_CURRENT_BINARY_DIR}/directusb_oboe")
add_subdirectory("${DIRECTUSB_ROOT}/driver" "${CMAKE_CURRENT_BINARY_DIR}/directusb_driver")

# Compile a genuine stock 1.10.0 factory/backend from the same source revision.
# Keep it out of the app's link dependencies: only the wrapper may dlopen it.
get_target_property(wrapper_sources oboe SOURCES)
set(stock_sources)
foreach(source IN LISTS wrapper_sources)
    string(FIND "${source}" "${OBOE_ROOT}/" prefix)
    if(prefix EQUAL 0)
        list(APPEND stock_sources "${source}")
    endif()
endforeach()
list(APPEND stock_sources
    "${OBOE_ROOT}/src/common/AudioStreamBuilder.cpp"
    "${OBOE_ROOT}/src/common/OboeExtensions.cpp")
add_library(oboeoriginal SHARED ${stock_sources})
target_include_directories(oboeoriginal PRIVATE "${OBOE_ROOT}/src" "${OBOE_ROOT}/include")
target_compile_options(oboeoriginal PRIVATE -O3 -Wno-sign-compare)
target_link_libraries(oboeoriginal PRIVATE log OpenSLES dl)
target_link_options(oboeoriginal PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--no-undefined")
