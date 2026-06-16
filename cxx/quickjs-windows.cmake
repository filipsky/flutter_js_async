set(CXX_LIB_DIR ${CMAKE_CURRENT_LIST_DIR})

# quickjs built as a static library so quickjs_c_bridge.dll links it directly
# without needing a separate quickjs.dll / quickjs.lib import library.
set(QUICK_JS_LIB_DIR ${CXX_LIB_DIR}/quickjs)
file(STRINGS "${QUICK_JS_LIB_DIR}/VERSION" QUICKJS_VERSION)
add_library(quickjs STATIC
    ${QUICK_JS_LIB_DIR}/cutils.c
    ${QUICK_JS_LIB_DIR}/libregexp.c
    ${QUICK_JS_LIB_DIR}/libunicode.c
    ${QUICK_JS_LIB_DIR}/quickjs.c
    ${QUICK_JS_LIB_DIR}/quickjs-debugger.c
    ${QUICK_JS_LIB_DIR}/quickjs-debugger-transport-win.c
)
target_compile_options(quickjs PRIVATE "-DCONFIG_VERSION=\"${QUICKJS_VERSION}\"")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(quickjs PRIVATE "-DDUMP_LEAKS")
endif()
target_link_libraries(quickjs PRIVATE ws2_32)