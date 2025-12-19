file(GLOB AIG_SRC *.cpp)

add_library(aig STATIC ${AIG_SRC})

target_include_directories(aig
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
