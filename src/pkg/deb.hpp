#pragma once
#include "../config.hpp"
#include <string>

std::string packDeb(const Config& c, const std::string& stage, const std::string& work, const std::string& outDir);
