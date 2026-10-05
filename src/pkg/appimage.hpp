#pragma once
#include "../config.hpp"
#include <string>

std::string appimageArch(const std::string& arch);
std::string runtimeFor(const std::string& arch);
bool validRuntime(const std::string& path, const std::string& arch);
std::string packAppImage(const Config& c, const std::string& stage, const std::string& work, const std::string& outDir);
