file(GLOB_RECURSE srcs "*.cpp" "*.c")
file(GLOB_RECURSE headers "*.h" "*.hpp")

set(inc_dirs "")
foreach(header ${headers})
    get_filename_component(dir ${header} DIRECTORY)
    file(RELATIVE_PATH rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${dir})
    list(APPEND inc_dirs ${rel_dir})
endforeach()
if(inc_dirs)
    list(REMOVE_DUPLICATES inc_dirs)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${inc_dirs}
    REQUIRES arduino wireguard_lwip nvs_flash json lwip esp_netif mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-w")
