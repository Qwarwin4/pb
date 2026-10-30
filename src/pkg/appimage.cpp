#include "appimage.hpp"
#include "../elf.hpp"
#include "../util.hpp"
#include "../lang.hpp"
#include "../proc.hpp"
#include "../squashfs.hpp"
#include "../stage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <zlib.h>
namespace fs = std::filesystem;

namespace {

void put32(std::string& s, uint32_t v) {
    for (int i = 24; i >= 0; i -= 8) s += (char)(v >> i);
}

void chunk(std::string& png, const char* type, const std::string& data) {
    put32(png, (uint32_t)data.size());
    std::string body = type + data;
    png += body;
    put32(png, (uint32_t)crc32(0, reinterpret_cast<const Bytef*>(body.data()), (uInt)body.size()));
}

float segDist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.f, 1.f);
    return std::hypot(px - ax - t * dx, py - ay - t * dy);
}

std::string terminalIcon() {
    const int n = 256;
    std::string raw;
    for (int y = 0; y < n; ++y) {
        raw += '\0';
        for (int x = 0; x < n; ++x) {
            float cx = std::clamp((float)x, 48.f, 208.f), cy = std::clamp((float)y, 64.f, 192.f);
            bool box = std::hypot(x - cx, y - cy) <= 24;
            bool glyph = segDist(x, y, 72, 92, 118, 128) <= 8 || segDist(x, y, 118, 128, 72, 164) <= 8 ||
                         segDist(x, y, 132, 168, 184, 168) <= 8;
            uint32_t px = !box ? 0 : glyph ? 0xf5f5f5ff : 0x263238ff;
            for (int i = 24; i >= 0; i -= 8) raw += (char)(px >> i);
        }
    }
    uLongf len = compressBound((uLong)raw.size());
    std::string z(len, '\0');
    compress2(reinterpret_cast<Bytef*>(z.data()), &len, reinterpret_cast<const Bytef*>(raw.data()), (uLong)raw.size(), 9);
    z.resize(len);

    std::string ihdr;
    put32(ihdr, n);
    put32(ihdr, n);
    ihdr += std::string("\x08\x06\x00\x00\x00", 5);
    std::string png = "\x89PNG\r\n\x1a\n";
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
    return png;
}

}

std::string appimageArch(const std::string& arch) {
    if (arch == "amd64") return "x86_64";
    if (arch == "arm64") return "aarch64";
    if (arch == "i386") return "i686";
    if (arch == "armhf") return "armhf";
    return "";
}

bool validRuntime(const std::string& path, const std::string& arch) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) < 64 * 1024) return false;
    auto info = inspectBinary(path);
    return info.elf && info.arch == arch;
}

std::string runtimeFor(const std::string& arch) {
    auto name = appimageArch(arch);
    if (name.empty()) throw std::runtime_error(lang::t("runtime_arch") + arch);

    if (const char* env = std::getenv("PB_APPIMAGE_RUNTIME"); env && *env) {
        if (!validRuntime(env, arch)) throw std::runtime_error(lang::t("runtime_bad") + env);
        return env;
    }

    auto path = cacheDir() + "/runtime-" + name;
    if (validRuntime(path, arch)) return path;

    fs::create_directories(cacheDir());
    auto part = path + ".part-" + std::to_string(getpid());
    auto url = "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-" + name;
    try {
        if (exists("curl")) must({"curl", "-fsSL", "--retry", "3", "-o", part, url});
        else if (exists("wget")) must({"wget", "-q", "--tries=3", "-O", part, url});
        else throw std::runtime_error(lang::t("need_curl_wget_runtime"));
    } catch (...) {
        std::error_code ec;
        fs::remove(part, ec);
        throw;
    }
    if (!validRuntime(part, arch)) {
        fs::remove(part);
        throw std::runtime_error(lang::t("runtime_bad") + url);
    }
    fs::permissions(part, exec755);
    fs::rename(part, path);
    return path;
}

std::string packAppImage(const Config& c, const std::string& stage, const std::string& work, const std::string& outDir) {
    auto arch = c.arch == "all" ? hostArch() : c.arch;
    auto runtime = runtimeFor(arch);

    auto appdir = work + "/AppDir";
    resetDir(appdir);
    fs::copy(stage + "/usr", appdir + "/usr", fs::copy_options::recursive);

    writeFile(appdir + "/AppRun", "#!/bin/sh\n"
                                  "HERE=\"${APPDIR:-$(dirname \"$(readlink -f \"$0\")\")}\"\n"
                                  "exec \"$HERE/usr/bin/" + c.binary + "\" \"$@\"\n");
    fs::permissions(appdir + "/AppRun", exec755);

    auto icon = c.name + ".png";
    if (auto own = iconPath(c); !own.empty()) {
        icon = c.name + fs::path(own).extension().string();
        fs::copy_file(stage + "/" + own, appdir + "/" + icon);
    } else {
        writeFile(appdir + "/" + icon, terminalIcon());
    }
    fs::create_symlink(icon, appdir + "/.DirIcon");
    writeFile(appdir + "/" + c.name + ".desktop", "[Desktop Entry]\n"
                                                  "Type=Application\n"
                                                  "Name=" + c.name + "\n"
                                                  "Comment=" + c.description + "\n"
                                                  "Exec=" + c.binary + "\n"
                                                  "Icon=" + c.name + "\n"
                                                  "Categories=" + c.categories + "\n"
                                                  "Terminal=" + (c.gui ? "false" : "true") + "\n"
                                                  "X-AppImage-Version=" + c.version + "\n");

    auto image = work + "/image.sqfs";
    writeSquashfs(appdir, image);

    fs::create_directories(outDir);
    auto out = fs::absolute(outDir + "/" + c.name + "-" + c.version + "-" + appimageArch(arch) + ".AppImage").string();
    auto part = out + ".part";
    {
        std::ofstream f(part, std::ios::binary | std::ios::trunc);
        std::ifstream rt(runtime, std::ios::binary), sq(image, std::ios::binary);
        f << rt.rdbuf() << sq.rdbuf();
        f.close();
        if (!f) {
            fs::remove(part);
            throw std::runtime_error(lang::t("cant_write") + out);
        }
    }
    fs::permissions(part, exec755);
    fs::rename(part, out);
    return out;
}
