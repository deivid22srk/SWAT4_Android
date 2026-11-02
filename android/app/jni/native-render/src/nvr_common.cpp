#include "nvr_common.h"
#include <cstdarg>
#include <sys/stat.h>
#include <unistd.h>

namespace nvr {

static LogLevel g_level = LogLevel::Info;
static bool g_level_init = false;

static void init_level() {
    if (g_level_init) return;
    g_level_init = true;
    const char *e = getenv("NVR_NR_LOG");
    if (e && *e) {
        if (!strcmp(e, "error")) g_level = LogLevel::Error;
        else if (!strcmp(e, "warn")) g_level = LogLevel::Warn;
        else if (!strcmp(e, "trace")) g_level = LogLevel::Trace;
        else g_level = LogLevel::Info;
    } else {
        std::string cs = Config::get().get("log", "info");
        const char *c = cs.c_str();
        if (!strcmp(c, "error")) g_level = LogLevel::Error;
        else if (!strcmp(c, "warn")) g_level = LogLevel::Warn;
        else if (!strcmp(c, "trace")) g_level = LogLevel::Trace;
        else g_level = LogLevel::Info;
    }
}

LogLevel log_level() { init_level(); return g_level; }

void log_line(LogLevel lvl, const char *fmt, ...) {
    init_level();
    if (lvl > g_level) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    const char *tag = lvl == LogLevel::Error ? "E " : lvl == LogLevel::Warn ? "W " : "I ";
    char full[1056];
    snprintf(full, sizeof full, "%s%s", tag, buf);
    NVR_LOG_NATIVE("%s", full);
}

void vk_check(VkResult r, const char *what, const char *file, int line) {
    if (r == VK_SUCCESS) return;
    NVR_ERROR("Vulkan %s falhou (%d) em %s:%d", what, (int)r, file, line);
    // Falha irrecuperavel de Vulkan: um abort limpo e mais uteil que estado
    // corrompido. O runtime do jogo registra last_exit.txt.
    abort();
}

// ------------------------------------------------------------------ config
Config &Config::get() { static Config c; return c; }

void Config::load(const char *dir) {
    if (!dir || !*dir) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/native-render.ini", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    std::lock_guard<std::mutex> l(m_mutex);
    while (fgets(line, sizeof line, f)) {
        char *h = line;
        while (*h == ' ' || *h == '\t') h++;
        if (*h == '#' || *h == ';' || *h == '\n' || !*h) continue;
        char *eq = strchr(h, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = h, *v = eq + 1;
        // trim
        while (*k == ' ') k++;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\n' || ke[-1] == '\r')) *--ke = 0;
        char *ve = v + strlen(v);
        while (ve > v && (ve[-1] == ' ' || ve[-1] == '\n' || ve[-1] == '\r')) *--ve = 0;
        if (*k && *v) m_map[k] = v;
    }
    fclose(f);
    NVR_INFO("config lida: %s (%zu chaves)", path, m_map.size());
}

std::string Config::get(const char *key, const char *fallback) const {
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_map.find(key);
    return it == m_map.end() ? std::string(fallback) : it->second;
}

int Config::get_int(const char *key, int fallback) const {
    std::string v = get(key, "");
    if (v.empty()) return fallback;
    return atoi(v.c_str());
}

bool Config::get_bool(const char *key, bool fallback) const {
    std::string v = get(key, "");
    if (v.empty()) return fallback;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

// ------------------------------------------------------------------- util
uint32_t fnv1a(const void *data, size_t len, uint32_t seed) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = seed;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static char g_cache_dir[512];

const char *cache_dir() { return g_cache_dir; }

void set_cache_dir(const char *dir) {
    if (!dir) { g_cache_dir[0] = 0; return; }
    snprintf(g_cache_dir, sizeof g_cache_dir, "%s", dir);
    mkdir(g_cache_dir, 0755); // best effort; mkdir recursivo ja feito pelo caller
}

} // namespace nvr
