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

std::string projName(Proj p) {
    switch (p) {
        case Proj::Cmake: return "CMake/C++";
        case Proj::Cargo: return "Rust (cargo)";
        case Proj::Go:    return "Go";
        case Proj::Make:  return "Make";
        default:          return "?";
    }
}
