#include "build.hpp"
#include "lang.hpp"
#include "proc.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;

namespace {

bool isExecutable(const fs::path& p) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    return !ec && fs::is_regular_file(st) && (st.permissions() & fs::perms::owner_exec) != fs::perms::none;
}

std::string search(const fs::path& root, const std::string& name) {
    std::error_code ec;
    fs::path best;
    int bestDepth = 1 << 30;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        auto fn = it->path().filename().string();
        if (it->is_directory() && (fn == ".git" || fn == "node_modules" || it.depth() > 6)) {
            it.disable_recursion_pending();
            continue;
        }
        if (fn == name && it.depth() < bestDepth && isExecutable(it->path())) {
            best = it->path();
            bestDepth = it.depth();
        }
    }
    return best.string();
}

std::string locate(const std::vector<fs::path>& expected, const fs::path& searchRoot, const std::string& name) {
    for (auto& p : expected)
        if (isExecutable(p)) return fs::absolute(p).string();
    auto found = search(searchRoot, name);
    if (found.empty()) throw std::runtime_error(lang::t("bin_missing") + (expected.front()).string());
    return fs::absolute(found).string();
}

bool hasMainPackage(const fs::path& dir) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        auto fn = e.path().filename().string();
        if (!e.is_regular_file() || e.path().extension() != ".go" ||
            (fn.size() > 8 && fn.rfind("_test.go") == fn.size() - 8))
            continue;
        std::ifstream in(e.path());
        std::string line;
        while (std::getline(in, line)) {
            auto s = line.find_first_not_of(" \t");
            if (s == std::string::npos || line.compare(s, 2, "//") == 0) continue;
            if (line.compare(s, 13, "package main") == 0 &&
                (line.size() == s + 12 || std::isspace((unsigned char)line[s + 12]) || line[s + 12] == '/'))
                return true;
            break;
        }
    }
    return false;
}

}

std::string goMain(const Config& c) {
    if (!c.main.empty()) return c.main;
    fs::path root = c.dir;
    if (hasMainPackage(root)) return ".";

    std::vector<std::string> found;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory()) continue;
        auto fn = it->path().filename().string();
        if (fn[0] == '.' || fn[0] == '_' || fn == "vendor" || fn == "testdata" || fn == "node_modules" ||
            it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        if (hasMainPackage(it->path())) found.push_back("./" + fs::relative(it->path(), root).generic_string());
    }
    std::sort(found.begin(), found.end());

    if (found.size() == 1) return found[0];
    for (auto& f : found)
        if (fs::path(f).filename() == c.binary) return f;
    if (found.empty()) throw std::runtime_error(lang::t("go_no_main") + c.dir);

    std::string list;
    for (auto& f : found) list += "\n  main = \"" + f + "\"";
    throw std::runtime_error(lang::t("go_many_main") + list);
}

std::string buildProj(Proj p, const Config& c) {
    const std::string& dir = c.dir;
    const std::string& bin = c.binary;
    fs::path d = dir;
    switch (p) {
        case Proj::Cmake:
            must({"cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"}, dir);
            must({"cmake", "--build", "build", "--config", "Release", "--parallel"}, dir);
            return locate({d / "build" / bin, d / "build" / "Release" / bin}, d / "build", bin);

        case Proj::Cargo: {
            must({"cargo", "build", "--release"}, dir);
            fs::path target = d / "target";
            if (const char* t = std::getenv("CARGO_TARGET_DIR"); t && *t)
                target = fs::path(t).is_absolute() ? fs::path(t) : d / t;
            return locate({target / "release" / bin}, target / "release", bin);
        }

        case Proj::Go:
            must({"go", "build", "-o", bin, goMain(c)}, dir);
            return locate({d / bin}, d, bin);

        case Proj::Make:
            must({"make"}, dir);
            return locate({d / bin, d / "build" / bin, d / "bin" / bin}, d, bin);

        default:
            throw std::runtime_error("unknown_lang");
    }
}
