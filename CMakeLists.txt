cmake_minimum_required(VERSION 3.22)

project(mini_query_engine)

set(CMAKE_CXX_STANDARD_REQUIRED 23)

include(FetchContent)

FetchContent_Declare(
    parsi
    GIT_REPOSITORY https://github.com/hexorer/parsi
    GIT_TAG main
)
FetchContent_MakeAvailable(parsi)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_link_libraries(${PROJECT_NAME} PRIVATE parsi::parsi)

add_executable(expriment experiment.cpp)
target_compile_features(expriment PRIVATE cxx_std_23)
target_link_libraries(expriment PRIVATE parsi::parsi)
