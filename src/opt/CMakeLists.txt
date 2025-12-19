file(GLOB OPT_SRC *.cpp)

add_library(opt STATIC ${OPT_SRC})

target_include_directories(opt
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(opt
    PUBLIC
        aig
)
