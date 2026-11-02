// Cabecalho comum do native-render: logging, config, utilidades.
#pragma once
#include <vulkan/vulkan.h>
#include <d3d9.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

#ifdef __ANDROID__
#include <android/log.h>
#define NVR_LOG_NATIVE(...) __android_log_print(ANDROID_LOG_INFO, "native-render", __VA_ARGS__)
#else
#define NVR_LOG_NATIVE(...) do { fprintf(stderr, "[native-render] " __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#endif

namespace nvr {

enum class LogLevel : int { Error = 0, Warn = 1, Info = 2, Trace = 3 };

// Nivel controlado por NVR_NR_LOG=error|warn|info|trace (default: info).
LogLevel log_level();
void log_line(LogLevel lvl, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define NVR_ERROR(...) ::nvr::log_line(::nvr::LogLevel::Error, __VA_ARGS__)
#define NVR_WARN(...)  ::nvr::log_line(::nvr::LogLevel::Warn,  __VA_ARGS__)
#define NVR_INFO(...)  ::nvr::log_line(::nvr::LogLevel::Info,  __VA_ARGS__)
#define NVR_TRACE(...) do { if (::nvr::log_level() >= ::nvr::LogLevel::Trace) \
                                ::nvr::log_line(::nvr::LogLevel::Trace, __VA_ARGS__); } while (0)

// Erro de Vulkan: registra e aborta o processo (mesma filosofia do runtime:
// falha de driver grafico no boot nao tem recuperacao razoavel; o runtime do
// jogo ja transforma D3DERR em caminhos de erro da UE2 quando aplicavel).
void vk_check(VkResult r, const char *what, const char *file, int line);
#define NVR_VK(x) ::nvr::vk_check((x), #x, __FILE__, __LINE__)

// ------------------------------------------------------------------ config
// Le <dir>/native-render.ini (chave=valor, # comentario). Chaves usadas:
//   log=error|warn|info|trace        (default info)
//   frames_in_flight=2|3             (default 2)
//   pipeline_cache=0|1               (default 1)
//   async_pipeline=0|1               (default 1)
//   force_bc=0|1                     (default: automatico por suporte do device)
//   prefer_discrete=0|1              (default 0; mobile tem 1 GPU)
class Config {
public:
    static Config &get();
    void load(const char *dir);
    std::string get(const char *key, const char *fallback) const;
    int get_int(const char *key, int fallback) const;
    bool get_bool(const char *key, bool fallback) const;
private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_map;
};

// ------------------------------------------------------------------- util
uint32_t fnv1a(const void *data, size_t len, uint32_t seed = 2166136261u);
inline uint32_t fnv1a_mix(uint32_t h, uint32_t v) { h ^= v; return h * 16777619u; }

// Diretorio de cache global (nvr_set_cache_dir). Retorna "" se nao definido.
const char *cache_dir();
void set_cache_dir(const char *dir);

// Se "condition" falha, loga com contexto e retorna hr informado.
#define NVR_FAIL(hr, ...) do { NVR_ERROR(__VA_ARGS__); return (hr); } while (0)

constexpr uint32_t kMaxStreams = 16;
constexpr uint32_t kMaxTextures = 16;      // SM3: s0..s15 no pixel shader
constexpr uint32_t kMaxTss = 8;            // estagios fixed-function
constexpr uint32_t kMaxVsc = 256;          // VS constant float4 (SM3)
constexpr uint32_t kMaxPsc = 224;          // PS constant float4 (SM3)
constexpr uint32_t kMaxIsc = 16;
constexpr uint32_t kMaxBsc = 16;
constexpr uint32_t kMaxLights = 8;
constexpr uint32_t kMaxClipPlanes = 6;     // mobile: reportado e real (planos clip)
constexpr uint32_t kFramesInFlightDefault = 2;

// Bindings do descriptor set 0 (unico layout, compartilhado por TODOS os
// pipelines: SM3, FFF e blit/clear do present). Vivem aqui porque tanto o
// emissor SPIR-V (shader/) quanto os caches de pipeline (vulkan/) precisam.
constexpr uint32_t kBindVsUbo = 0;         // UBO dinamico VS (constants)
constexpr uint32_t kBindPsUbo = 1;         // UBO dinamico PS (constants + sys)
constexpr uint32_t kBindSamplerBase = 2;   // s0..s15 (combined image sampler)
constexpr uint32_t kBindSsbo = 18;         // SSBO (ProcessVertices)
constexpr uint32_t kBindIbUbo = 19;        // UBO dinamico constantes i#/b#

} // namespace nvr
