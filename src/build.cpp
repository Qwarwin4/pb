#include "build.hpp"
#include "lang.hpp"
#include "proc.hpp"
#include <cstdlib>
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

}

std::string buildProj(Proj p, const std::string& dir, const std::string& bin) {
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
            must({"go", "build", "-o", bin, "."}, dir);
            return locate({d / bin}, d, bin);

        case Proj::Make:
            must({"make"}, dir);
            return locate({d / bin, d / "build" / bin, d / "bin" / bin}, d, bin);

        default:
            throw std::runtime_error("unknown_lang");
    }
}
