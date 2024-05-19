cmake_minimum_required(VERSION 3.8)

project("random-nickname-test")

add_executable(${PROJECT_NAME} src/main.cc)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_include_directories(${PROJECT_NAME} PRIVATE "external/spdlog/include")
