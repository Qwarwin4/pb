#include "aur.hpp"
#include "../archive.hpp"
#include "../fsutil.hpp"
#include "../lang.hpp"
#include "../proc.hpp"
#include "../sha256.hpp"
#include "../stage.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;

std::string shellQuote(const std::string& s) {
    std::string r = "'";
    for (char ch : s) r += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return r + "'";
}

namespace {

struct Source {
    std::string entry;
    std::string sum;
};

std::string originUrl(const std::string& dir) {
    if (!exists("git")) return "";
    std::string url;
    try {
        url = capture({"git", "-C", dir, "remote", "get-url", "origin"}, true);
    } catch (...) {
        return "";
    }
    while (!url.empty() && (url.back() == '\n' || url.back() == '\r' || url.back() == ' ')) url.pop_back();
    if (url.rfind("git@", 0) == 0) {
        auto colon = url.find(':');
        if (colon == std::string::npos) return "";
        url = "https://" + url.substr(4, colon - 4) + "/" + url.substr(colon + 1);
    }
    if (url.rfind("https://", 0) != 0 || url.find_first_of(" '\"$`\\") != std::string::npos) return "";
    return url;
}

std::string defaultBinRel(Proj p, const std::string& bin) {
    switch (p) {
        case Proj::Cmake: return "build/" + bin;
        case Proj::Cargo: return "target/release/" + bin;
        default: return bin;
    }
}

std::string buildFn(Proj p, const Config& c, bool locked) {
    switch (p) {
        case Proj::Cmake:
            return "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\n"
                   "  cmake --build build\n";
        case Proj::Cargo:
            return std::string("  export CARGO_TARGET_DIR=target\n  cargo build --release") +
                   (locked ? " --locked" : "") + "\n";
        case Proj::Go:
            return "  export CGO_CPPFLAGS=\"$CPPFLAGS\" CGO_CFLAGS=\"$CFLAGS\" CGO_CXXFLAGS=\"$CXXFLAGS\" CGO_LDFLAGS=\"$LDFLAGS\"\n"
                   "  export GOFLAGS=\"-buildmode=pie -trimpath -mod=readonly -modcacherw\"\n"
                   "  go build -o " + shellQuote(c.binary) + " .\n";
        case Proj::Make:
            return "  make\n";
        default:
            throw std::runtime_error("unknown_lang");
    }
}

std::string makedep(Proj p) {
    switch (p) {
        case Proj::Cmake: return "cmake";
        case Proj::Cargo: return "cargo";
        case Proj::Go: return "go";
        default: return "";
    }
}

void write(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::trunc);
    f << data;
    f.close();
    if (!f) throw std::runtime_error(lang::t("cant_write") + path);
}

std::vector<std::string> skipList(const Config& c, const std::string& outDir, const std::string& binRel) {
    std::vector<std::string> skip = {".git", "build", "target", "stage", "dist", "pb-builds"};
    if (!binRel.empty()) skip.push_back(fs::path(binRel).lexically_normal().generic_string());
    auto out = fs::weakly_canonical(outDir).string();
    if (out.rfind(c.dir + "/", 0) == 0) {
        auto rel = out.substr(c.dir.size() + 1);
        skip.push_back(rel.substr(0, rel.find('/')));
    }
    return skip;
}

}

std::string packAur(const Config& c, Proj proj, const std::string& binRel, const std::string& stage,
                    const std::string& outDir) {
    if (proj == Proj::Unknown) throw std::runtime_error("unknown_lang");

    auto dir = outDir + "/" + c.name;
    resetDir(dir);

    bool git = fs::exists(c.dir + "/.git");
    std::string pkgname = git ? c.name + "-git" : c.name;
    std::string pkgver = git ? c.version + ".r0" : c.version;
    std::string src = git ? c.name : c.name + "-" + c.version;
    std::vector<std::string> arches = c.arch == "all" ? std::vector<std::string>{"any"}
                                                      : std::vector<std::string>{"x86_64", "aarch64"};

    std::vector<std::string> makedeps;
    if (auto d = makedep(proj); !d.empty()) makedeps.push_back(d);
    if (git) makedeps.push_back("git");

    std::vector<Source> sources;
    if (git) {
        auto url = originUrl(c.dir);
        if (url.empty()) url = "file://" + c.dir;
        sources.push_back({c.name + "::git+" + url, "SKIP"});
    } else {
        auto tar = src + ".tar.gz";
        writeTarGz(c.dir, dir + "/" + tar, src + "/", skipList(c, outDir, binRel));
        sources.push_back({tar, sha256File(dir + "/" + tar)});
    }

    std::string pkg;
    if (c.gui) {
        auto desktop = c.name + ".desktop";
        fs::copy_file(stage + "/" + desktopPath(c), dir + "/" + desktop);
        sources.push_back({desktop, sha256File(dir + "/" + desktop)});
        pkg += "  install -Dm644 \"$srcdir/" + desktop + "\" \"$pkgdir/usr/share/applications/" + desktop + "\"\n";
        if (auto icon = iconPath(c); !icon.empty()) {
            auto file = fs::path(icon).filename().string();
            fs::copy_file(stage + "/" + icon, dir + "/" + file);
            sources.push_back({file, sha256File(dir + "/" + file)});
            pkg += "  install -Dm644 \"$srcdir/" + file + "\" \"$pkgdir/" + icon + "\"\n";
        }
    }

    auto rel = binRel.empty() ? defaultBinRel(proj, c.binary) : binRel;
    bool locked = fs::exists(c.dir + "/Cargo.lock");

    auto list = [](const std::vector<std::string>& v) {
        std::string s;
        for (auto& x : v) s += (s.empty() ? "" : " ") + shellQuote(x);
        return s;
    };
    std::vector<std::string> entries, sums;
    for (auto& s : sources) {
        entries.push_back(s.entry);
        sums.push_back(s.sum);
    }

    std::string pb = "# Maintainer: " + c.maintainer + "\n"
                     "pkgname=" + pkgname + "\n"
                     "pkgver=" + pkgver + "\n"
                     "pkgrel=1\n"
                     "pkgdesc=" + shellQuote(c.description) + "\n"
                     "arch=(" + list(arches) + ")\n";
    if (!c.url.empty()) pb += "url=" + shellQuote(c.url) + "\n";
    pb += "license=(" + shellQuote(c.license) + ")\n";
    if (!makedeps.empty()) pb += "makedepends=(" + list(makedeps) + ")\n";
    if (git) pb += "provides=(" + shellQuote(c.name) + ")\nconflicts=(" + shellQuote(c.name) + ")\n";
    pb += "source=(" + list(entries) + ")\n"
          "sha256sums=(" + list(sums) + ")\n\n";
    if (git)
        pb += "pkgver() {\n  cd \"$srcdir/" + src + "\"\n"
              "  printf '%s.r%s.g%s' " + shellQuote(c.version) +
              " \"$(git rev-list --count HEAD)\" \"$(git rev-parse --short HEAD)\"\n}\n\n";
    pb += "build() {\n  cd \"$srcdir/" + src + "\"\n" + buildFn(proj, c, locked) + "}\n\n"
          "package() {\n  cd \"$srcdir/" + src + "\"\n"
          "  install -Dm755 " + shellQuote(rel) + " \"$pkgdir/usr/bin/" + c.binary + "\"\n" + pkg + "}\n";
    write(dir + "/PKGBUILD", pb);

    std::string si = "pkgbase = " + pkgname + "\n"
                     "\tpkgdesc = " + c.description + "\n"
                     "\tpkgver = " + pkgver + "\n"
                     "\tpkgrel = 1\n";
    if (!c.url.empty()) si += "\turl = " + c.url + "\n";
    for (auto& a : arches) si += "\tarch = " + a + "\n";
    si += "\tlicense = " + c.license + "\n";
    for (auto& d : makedeps) si += "\tmakedepends = " + d + "\n";
    if (git) si += "\tprovides = " + c.name + "\n\tconflicts = " + c.name + "\n";
    for (auto& e : entries) si += "\tsource = " + e + "\n";
    for (auto& s : sums) si += "\tsha256sums = " + s + "\n";
    si += "\npkgname = " + pkgname + "\n";
    write(dir + "/.SRCINFO", si);

    return dir;
}
