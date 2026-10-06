#pragma once
#include <string>

enum class Proj { Cmake, Cargo, Go, Make, Unknown };

Proj detectProj(const std::string& dir);
std::string projName(Proj p);
std::string projRootAbove(const std::string& dir);
