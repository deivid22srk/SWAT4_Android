# Recompiled game code (gen/) plus the runtime, linked into libmain.so.
# gen/ is generated from the user's own copy of the game and is never in git;
# CI uses ci/gen_stub (empty tables) for the app-only build.
file(GLOB GEN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../gen/*.c)
if(NOT GEN_SRC)
    message(FATAL_ERROR "no gen/*.c: run tools/regen_gen.sh, or copy ci/gen_stub into gen/ for an app-only build")
endif()

file(GLOB RT_C_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../runtime/*.c)
file(GLOB RT_CXX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../runtime/*.cpp)

add_library(main SHARED ${GEN_SRC} ${RT_C_SRC} ${RT_CXX_SRC})

# Generated code is lowered at -O1 (it is straight-line C with huge functions;
# -O2 takes far longer for little gain). 1-2 GB of RAM per file while compiling.
set_source_files_properties(${GEN_SRC} PROPERTIES COMPILE_OPTIONS "-O1;-w")
set_source_files_properties(${RT_C_SRC} PROPERTIES COMPILE_OPTIONS "-O2;-Wall;-Wno-unused-function")
set_source_files_properties(${RT_CXX_SRC} PROPERTIES COMPILE_OPTIONS "-O2")

target_compile_definitions(main PRIVATE GAME_SWAT4=1)
target_compile_options(main PRIVATE -fno-strict-aliasing -fwrapv)
# Generated code runs with the guest's x87 80-bit doubles and relies on
# wrapping arithmetic and aliasing-free unions.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing -fwrapv")

target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../gen
    ${CMAKE_CURRENT_SOURCE_DIR}/../sdlinc
    ${CMAKE_CURRENT_SOURCE_DIR}/../dxvk/include/native/directx
    ${CMAKE_CURRENT_SOURCE_DIR}/../dxvk/include/native/windows)

set_property(SOURCE ${GEN_SRC} PROPERTY JOB_POOL_COMPILE gen)
target_link_libraries(main PRIVATE SDL2 dxvk_d3d9 android log)
