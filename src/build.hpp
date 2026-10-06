#pragma once
#include "config.hpp"
#include "detect.hpp"
#include <string>

std::string goMain(const Config& c);
std::string buildProj(Proj p, const Config& c);
