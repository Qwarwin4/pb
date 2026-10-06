#include "detect.hpp"
#include <filesystem>
namespace fs = std::filesystem;

Proj detectProj(const std::string& dir) {
    if (fs::exists(dir + "/CMakeLists.txt")) return Proj::Cmake;
    if (fs::exists(dir + "/Cargo.toml"))     return Proj::Cargo;
    if (fs::exists(dir + "/go.mod"))         return Proj::Go;
    if (fs::exists(dir + "/Makefile"))       return Proj::Make;
    return Proj::Unknown;
}

std::string projRootAbove(const std::string& dir) {
    auto p = std::filesystem::absolute(dir).lexically_normal();
    for (int i = 0; i < 4 && p.has_parent_path() && p.parent_path() != p; ++i) {
        p = p.parent_path();
        if (detectProj(p.string()) != Proj::Unknown) return p.string();
    }
    return "";
}

std::string projName(Proj p) {
    switch (p) {
        case Proj::Cmake: return "CMake/C++";
        case Proj::Cargo: return "Rust (cargo)";
        case Proj::Go:    return "Go";
        case Proj::Make:  return "Make";
        default:          return "?";
    }
}
