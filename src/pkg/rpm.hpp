#pragma once
#include "../config.hpp"
#include <string>

std::string rpmArch(const std::string& arch);
std::string packRpm(const Config& c, const std::string& stage, const std::string& work, const std::string& outDir);
