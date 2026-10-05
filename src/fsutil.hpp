#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

inline std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h && *h ? h : ".";
}

inline std::string xdgDir(const char* var, const char* fallback) {
    const char* x = std::getenv(var);
    return (x && *x == '/' ? std::string(x) : homeDir() + fallback) + "/pb";
}

inline std::string configDir() { return xdgDir("XDG_CONFIG_HOME", "/.config"); }
inline std::string cacheDir() { return xdgDir("XDG_CACHE_HOME", "/.cache"); }

inline void resetDir(const std::filesystem::path& p) {
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
}

class TempDir {
public:
    TempDir(const std::string& parent, const std::string& prefix) {
        std::filesystem::create_directories(parent);
        std::string tpl = parent + "/" + prefix + "-XXXXXX";
        if (!mkdtemp(tpl.data())) throw std::runtime_error("mkdtemp: " + parent);
        path_ = tpl;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

inline void replaceFile(const std::string& src, const std::string& dst, std::filesystem::perms mode) {
    namespace fs = std::filesystem;
    std::string tmp = dst + ".new-" + std::to_string(getpid());
    fs::copy_file(src, tmp, fs::copy_options::overwrite_existing);
    fs::permissions(tmp, mode);
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) {
        fs::remove(tmp);
        throw std::runtime_error(dst + ": " + ec.message());
    }
}
