#pragma once
#include "config.hpp"
#include <string>

enum class Proj { Cmake, Cargo, Go, Make, Unknown };

Proj detectProj(const std::string& dir);
std::string projName(Proj p);
std::string projRootAbove(const std::string& dir);
std::string goMain(const Config& c);
std::string buildProj(Proj p, const Config& c);
