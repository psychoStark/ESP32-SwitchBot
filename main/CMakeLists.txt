idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES arduino microlink ESP32Servo
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=format")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ip4_route_src_hook")