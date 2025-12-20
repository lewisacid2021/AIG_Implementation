cmake_minimum_required(VERSION 3.16)
project(AIG_Project LANGUAGES CXX)

# ------------------------------
# 基本配置
# ------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 开启所有警告
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# ------------------------------
# 包含目录
# ------------------------------
include_directories(${PROJECT_SOURCE_DIR}/include)

# ------------------------------
# 自动收集 src 目录下的所有 .cpp 文件
# ------------------------------
file(GLOB_RECURSE SRC_FILES
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# 指定所有可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ------------------------------
# 添加可执行文件
# ------------------------------
add_executable(read_aig
    src/main.cpp      # 主入口
    ${SRC_FILES}      # 自动收集的其他 cpp
)
