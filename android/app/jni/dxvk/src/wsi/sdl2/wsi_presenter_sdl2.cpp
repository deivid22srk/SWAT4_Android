#include "../wsi_presenter.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <wsi/native_wsi.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

namespace dxvk::wsi {

  VkResult createSurface(
          HWND                hWindow,
    const Rc<vk::InstanceFn>& vki,
          VkSurfaceKHR*       pSurface) {
    SDL_Window* window = fromHwnd(hWindow);

    if (!SDL_Vulkan_CreateSurface(window, vki->instance(), pSurface)) {
      Logger::err(str::format("SDL2 WSI: SDL_Vulkan_CreateSurface failed: ",
                              SDL_GetError(), " (window flags ",
                              SDL_GetWindowFlags(window), ")"));
      return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return VK_SUCCESS;
  }

}