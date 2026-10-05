#pragma once
#include "config.hpp"
#include <string>

void makeStage(const Config& c, const std::string& bin, const std::string& root);
std::string desktopPath(const Config& c);
std::string iconPath(const Config& c);
