add_subdirectory(aig)
add_subdirectory(opt)

# 如果你有 main.cpp
add_executable(aig_opt_main main.cpp)

target_link_libraries(aig_opt_main
    PRIVATE
        aig
        opt
)
