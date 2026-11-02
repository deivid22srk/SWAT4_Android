# Small Vulkan loader/device probe used to record real Android GPU capabilities.
# This is deliberately separate from DXVK and does not replace the D3D9 backend.
add_library(nativevk_probe SHARED nativevk_probe.cpp)
target_compile_features(nativevk_probe PRIVATE cxx_std_17)
target_compile_options(nativevk_probe PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(nativevk_probe PRIVATE vulkan log)
