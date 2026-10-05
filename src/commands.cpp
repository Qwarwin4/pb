#include "commands.hpp"
#include "elf.hpp"
#include "fsutil.hpp"
#include "lang.hpp"
#include "proc.hpp"
#include "sha256.hpp"
#include "version.hpp"
#include "pkg/appimage.hpp"
#include <chrono>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace fs = std::filesystem;

namespace {

const std::string DST = "/usr/local/bin/pb";
const auto exec755 = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                     fs::perms::others_read | fs::perms::others_exec;

std::string selfPath() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) throw std::runtime_error("/proc/self/exe");
    return std::string(buf, (size_t)n);
}

bool writable(const std::string& p) { return access(p.c_str(), W_OK) == 0; }

int needRoot(const std::string& what) {
    std::cerr << lang::t("error") << lang::t("need_root") << what << "\n" << lang::t("need_root_hint") << "\n";
    return 1;
}

bool fromPackageManager(const std::string& self) { return self.rfind("/usr/bin/", 0) == 0; }

std::string releasesUrl() { return std::string("https://github.com/") + PB_REPO + "/releases/latest"; }

int install() {
    auto dir = fs::path(DST).parent_path().string();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!writable(dir)) return needRoot(dir);

    auto self = selfPath();
    if (fs::exists(DST) && fs::equivalent(self, DST, ec)) {
        std::cout << lang::t("already") << "\n";
        return 0;
    }
    replaceFile(self, DST, exec755);
    std::cout << lang::t("installed") << DST << "\n";
    return 0;
}

int uninstall(bool purge) {
    if (fs::exists(DST)) {
        if (!writable(fs::path(DST).parent_path().string())) return needRoot(DST);
        fs::remove(DST);
        std::cout << lang::t("removed") << "\n";
    } else if (auto self = selfPath(); fromPackageManager(self)) {
        std::cerr << lang::t("managed") << self << "\n";
    } else {
        std::cout << lang::t("not_installed") << DST << "\n";
    }

    if (purge) {
        std::error_code ec;
        fs::remove_all(configDir(), ec);
        fs::remove_all(cacheDir(), ec);
        std::cout << lang::t("purged") << "\n";
    }
    return 0;
}

int repair() {
    bool touched = false;
    std::error_code ec;

    if (fs::exists(DST) && (fs::status(DST).permissions() & exec755) != exec755) {
        if (!writable(fs::path(DST).parent_path().string())) return needRoot(DST);
        fs::permissions(DST, exec755);
        std::cout << lang::t("repair_perm") << DST << "\n";
        touched = true;
    }

    auto lf = lang::file();
    if (fs::exists(lf)) {
        std::ifstream in(lf);
        std::string code;
        std::getline(in, code);
        if (!writable(lf) || !writable(configDir())) {
            std::cerr << lang::t("repair_lang_ro") << lf << "\n";
            touched = true;
        } else if (code != "ru" && code != "en") {
            lang::set("ru");
            std::cout << lang::t("repair_lang") << "\n";
            touched = true;
        }
    }

    if (fs::is_directory(cacheDir())) {
        for (auto& e : fs::directory_iterator(cacheDir(), ec)) {
            auto name = e.path().filename().string();
            if (name.rfind("runtime-", 0) != 0 || name.find(".part") != std::string::npos) continue;
            std::string arch;
            for (auto a : {"amd64", "arm64", "i386", "armhf"})
                if (name == "runtime-" + appimageArch(a)) arch = a;
            if (!arch.empty() && !validRuntime(e.path().string(), arch)) {
                fs::remove(e.path(), ec);
                std::cout << lang::t("repair_cache") << e.path().string() << "\n";
                touched = true;
            }
        }
        auto stale = std::chrono::hours(24);
        for (auto& e : fs::directory_iterator(cacheDir() + "/work", ec)) {
            auto age = fs::file_time_type::clock::now() - e.last_write_time(ec);
            if (!ec && age > stale) fs::remove_all(e.path(), ec);
        }
    }

    if (!touched) std::cout << lang::t("repair_ok") << "\n";
    return 0;
}

std::vector<long> versionParts(const std::string& v) {
    std::vector<long> out;
    std::stringstream ss(v);
    std::string part;
    while (std::getline(ss, part, '.')) out.push_back(std::strtol(part.c_str(), nullptr, 10));
    return out;
}

std::string fetch(const std::string& url) {
    try {
        if (exists("curl")) return capture({"curl", "-fsSL", "--retry", "2", url}, true);
        if (exists("wget")) return capture({"wget", "-qO-", "--tries=2", url}, true);
    } catch (const std::exception&) {
        throw std::runtime_error(lang::t("fetch_failed"));
    }
    throw std::runtime_error(lang::t("need_curl_wget"));
}

void download(const std::string& url, const std::string& to) {
    if (exists("curl")) must({"curl", "-fsSL", "--retry", "2", "-o", to, url});
    else must({"wget", "-q", "--tries=2", "-O", to, url});
}

int update(bool yes) {
    if (const char* ai = std::getenv("APPIMAGE"); ai && *ai) {
        std::cout << lang::t("appimage_update") << releasesUrl() << "\n";
        return 1;
    }
    auto self = selfPath();
    if (fromPackageManager(self)) {
        std::cerr << lang::t("managed") << self << "\n";
        return 1;
    }

    std::cout << lang::t("checking") << PB_REPO << std::endl;
    auto json = fetch(std::string("https://api.github.com/repos/") + PB_REPO + "/releases/latest");
    std::smatch m;
    if (!std::regex_search(json, m, std::regex("\"tag_name\"\\s*:\\s*\"([^\"]+)\"")))
        throw std::runtime_error(lang::t("no_tag"));
    std::string tag = m[1].str();
    std::string ver = tag[0] == 'v' ? tag.substr(1) : tag;

    auto remote = versionParts(ver), local = versionParts(PB_VERSION);
    if (remote == local) {
        std::cout << lang::t("up_to_date") << PB_VERSION << "\n";
        return 0;
    }
    if (remote < local) {
        std::cout << lang::t("local_newer") << PB_VERSION << " > " << ver << "\n";
        return 0;
    }

    std::cout << lang::t("new_version") << ver << lang::t("current") << PB_VERSION << "\n";
    if (!yes) {
        if (!isatty(STDIN_FILENO)) {
            std::cerr << lang::t("need_yes") << "\n";
            return 1;
        }
        std::cout << lang::t("confirm") << std::flush;
        std::string ans;
        std::getline(std::cin, ans);
        if (ans != "y" && ans != "Y") return 0;
    }

    if (!writable(fs::path(self).parent_path().string())) return needRoot(self);

    auto arch = appimageArch(hostArch());
    auto asset = "pb-" + ver + "-" + arch;
    auto base = std::string("https://github.com/") + PB_REPO + "/releases/download/" + tag + "/";

    std::string expected;
    std::stringstream sums(fetch(base + "sha256sums.txt"));
    std::string line;
    while (std::getline(sums, line)) {
        auto sp = line.find("  ");
        if (sp != std::string::npos && line.substr(sp + 2) == asset) expected = line.substr(0, sp);
    }
    if (expected.empty()) throw std::runtime_error(lang::t("no_asset") + asset);

    TempDir tmp(cacheDir(), "update");
    auto file = tmp.path() + "/" + asset;
    std::cout << lang::t("downloading") << asset << std::endl;
    download(base + asset, file);
    if (sha256File(file) != expected) throw std::runtime_error(lang::t("bad_checksum"));

    fs::permissions(file, exec755);
    std::string out;
    try {
        out = capture({file, "--version"}, true);
    } catch (...) {
    }
    if (out.find(ver) == std::string::npos) throw std::runtime_error(lang::t("bad_binary"));

    replaceFile(file, self, exec755);
    std::cout << lang::t("updated") << ver << "\n";
    return 0;
}

template <typename F>
int guarded(F fn) {
    try {
        return fn();
    } catch (const std::exception& e) {
        std::cerr << lang::t("error") << e.what() << "\n";
        return 1;
    }
}

}

int cmdInstall() { return guarded(install); }
int cmdUninstall(bool purge) { return guarded([purge] { return uninstall(purge); }); }
int cmdRepair() { return guarded(repair); }
int cmdUpdate(bool yes) { return guarded([yes] { return update(yes); }); }
