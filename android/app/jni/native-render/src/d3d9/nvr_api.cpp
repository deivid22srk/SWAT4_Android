// Pontos de entrada do native-render.
#include "nvr_device.h"

namespace nvr {
NrDevice *g_active_device = nullptr;
NrDevice *g_current_device() { return g_active_device; }
void g_set_device(NrDevice *d) { g_active_device = d; }
} // namespace nvr

extern "C" {

void *nvr_Direct3DCreate9(uint32_t sdk_version) {
    (void)sdk_version;
    // A instancia Vulkan nasce na primeira consulta/CreateDevice (o SDL window
    // ja existe nesse ponto — mesmo modelo do dxvk-native).
    auto *d3d = new nvr::NrD3D9();
    nvr::log_line(nvr::LogLevel::Info, "nvr_Direct3DCreate9(%u)", sdk_version);
    return d3d;
}

void nvr_set_cache_dir(const char *dir) { nvr::set_cache_dir(dir); }

const char *nvr_version_string(void) { return "native-render 0.1.0 (Vulkan 1.1)"; }
}
