#include "appimage.hpp"
#include "../elf.hpp"
#include "../fsutil.hpp"
#include "../lang.hpp"
#include "../proc.hpp"
#include "../squashfs.hpp"
#include "../stage.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
namespace fs = std::filesystem;

namespace {

const auto exec755 = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                     fs::perms::others_read | fs::perms::others_exec;

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

    {
        std::ofstream run(appdir + "/AppRun");
        run << "#!/bin/sh\n"
            << "HERE=\"${APPDIR:-$(dirname \"$(readlink -f \"$0\")\")}\"\n"
            << "exec \"$HERE/usr/bin/" << c.binary << "\" \"$@\"\n";
        run.close();
        if (!run) throw std::runtime_error(lang::t("cant_write") + appdir + "/AppRun");
    }
    fs::permissions(appdir + "/AppRun", exec755);

    if (c.gui) {
        fs::copy_file(stage + "/" + desktopPath(c), appdir + "/" + c.name + ".desktop");
        if (auto icon = iconPath(c); !icon.empty())
            fs::copy_file(stage + "/" + icon, appdir + "/" + c.name + fs::path(icon).extension().string());
    }

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
