cmake_minimum_required(VERSION 3.16) # minimum CMake requirements to build
project(Poker LANGUAGES CXX) # name and enable C++ 

set(CMAKE_CXX_STANDARD 20) # C++20 language
set(TRAINING_ITERATIONS 100000 CACHE STRING "Number of CFR Training Iterations for each Poker Game")

find_package(OpenMP REQUIRED)

add_library(generate_histories INTERFACE)
target_include_directories(generate_histories INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# builds solver.cpp
add_executable(nodemap_generator src/cfr/solver.cpp)
# adds the source root so the generator can resolve project headers
target_include_directories(nodemap_generator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(nodemap_generator PRIVATE generate_histories OpenMP::OpenMP_CXX)
# folder build/generated
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
# file generated/nodemap.bin
set(GENERATED_MAP_SOURCE "${GENERATED_DIR}/nodemap.bin")
# runs generate_nodemaps code
add_custom_command(
    OUTPUT "${GENERATED_MAP_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "$<TARGET_FILE:nodemap_generator>"
            "${TRAINING_ITERATIONS}"
            "${GENERATED_MAP_SOURCE}"
    DEPENDS nodemap_generator
    COMMENT "Training: ${TRAINING_ITERATIONS} iterations each"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(generate_nodemaps DEPENDS "${GENERATED_MAP_SOURCE}")

add_executable(main src/main.cpp)
add_dependencies(main generate_nodemaps)
target_compile_definitions(main PRIVATE POKER_NODEMAP_PATH="${GENERATED_MAP_SOURCE}")
target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(main PRIVATE generate_histories OpenMP::OpenMP_CXX)

add_executable(generate_histograms data/generate_histograms.cpp)
target_include_directories(generate_histograms PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(generate_histograms PRIVATE OpenMP::OpenMP_CXX)

add_executable(play src/engine/engine.cpp)
add_dependencies(play generate_nodemaps)
target_compile_definitions(play PRIVATE POKER_NODEMAP_PATH="${GENERATED_MAP_SOURCE}")
target_include_directories(play PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(play PRIVATE generate_histories OpenMP::OpenMP_CXX)
