# Native Vulkan probe

This Android-only library records Vulkan loader and physical-device capabilities to help design a future native renderer.

At game startup, `GameActivity` writes the report to:

`<selected-game-folder>/port/native_vulkan_probe.txt`

The probe records loader/device API versions, physical device identity, driver version, graphics queue families, device-local memory heaps, and selected core features.

It is diagnostic only. It does not create a logical device, surface, swapchain, command buffers, pipelines, or render a frame. DXVK remains the active D3D9 implementation and fallback.

The report should be collected from representative Android devices before implementing D3D9 feature translation. A successful probe is not proof that a device can run the complete SWAT 4 rendering workload.
