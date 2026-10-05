#pragma once
#include "../config.hpp"
#include "../detect.hpp"
#include <string>

std::string shellQuote(const std::string& s);
std::string packAur(const Config& c, Proj proj, const std::string& binRel, const std::string& stage,
                    const std::string& outDir);
