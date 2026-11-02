# native-render: backend Vulkan proprio do port SWAT4 Android.
# Alvo: libnative_render.so (arm64-v8a no CI; x86-64 nos testes desktop).
cmake_minimum_required(VERSION 3.18)
project(native_render CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Caminhos relativos a ESTE CMakeLists (native-render/src/).
set(NVR_SRC
    nvr_common.cpp
    nvr_format.cpp
    vulkan/nvr_vk.cpp
    vulkan/nvr_vma_impl.cpp
    vulkan/nvr_gfx.cpp
    vulkan/nvr_fb.cpp
    shader/nvr_spirv.cpp
    shader/nvr_dx9bc.cpp
    shader/nvr_compile.cpp
    shader/nvr_fff.cpp
    d3d9/nvr_d3d9.cpp
    d3d9/nvr_device.cpp
    d3d9/nvr_device2.cpp
    d3d9/nvr_res.cpp
    d3d9/nvr_draw.cpp
    d3d9/nvr_draw2.cpp
    d3d9/nvr_pack.cpp
    d3d9/nvr_present.cpp
    d3d9/nvr_api.cpp
)

add_library(native_render SHARED ${NVR_SRC})

# thirdparty/vma e o dxvk/sdlinc ficam um nivel ACIMA deste CMakeLists
# (native-render/thirdparty, jni/dxvk, jni/sdlinc).
target_include_directories(native_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/vma
    ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/spirv_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dxvk/include/native/directx
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dxvk/include/native/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dxvk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdlinc
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
