# native-render: backend Vulkan proprio do port SWAT4 Android.
# Alvo: libnative_render.so (arm64-v8a no CI; x86-64 nos testes desktop).
cmake_minimum_required(VERSION 3.18)
project(native_render CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(NVR_SRC
    src/nvr_common.cpp
    src/nvr_format.cpp
    src/vulkan/nvr_vk.cpp
    src/vulkan/nvr_vma_impl.cpp
    src/vulkan/nvr_gfx.cpp
    src/vulkan/nvr_fb.cpp
    src/shader/nvr_spirv.cpp
    src/shader/nvr_dx9bc.cpp
    src/shader/nvr_compile.cpp
    src/shader/nvr_fff.cpp
    src/d3d9/nvr_d3d9.cpp
    src/d3d9/nvr_device.cpp
    src/d3d9/nvr_device2.cpp
    src/d3d9/nvr_res.cpp
    src/d3d9/nvr_draw.cpp
    src/d3d9/nvr_draw2.cpp
    src/d3d9/nvr_pack.cpp
    src/d3d9/nvr_present.cpp
    src/d3d9/nvr_api.cpp
)

add_library(native_render SHARED ${NVR_SRC})

target_include_directories(native_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vma
    ${CMAKE_CURRENT_SOURCE_DIR}/../dxvk/include/native/directx
    ${CMAKE_CURRENT_SOURCE_DIR}/../dxvk/include/native/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/../sdlinc
)

find_package(Vulkan)
if (Vulkan_FOUND)
    target_link_libraries(native_render PRIVATE Vulkan::Vulkan)
else()
    target_link_libraries(native_render PRIVATE vulkan)
endif()

if (ANDROID)
    target_link_libraries(native_render PRIVATE android log)
endif()
target_compile_options(native_render PRIVATE -Wall -Wno-unused-variable -Wno-unused-but-set-variable)
