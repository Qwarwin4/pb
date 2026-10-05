#pragma once
#include <string>
#include <vector>

std::string findInPath(const std::string& tool);
bool exists(const std::string& tool);

int run(const std::vector<std::string>& args, const std::string& cwd = "", bool quiet = false);
void must(const std::vector<std::string>& args, const std::string& cwd = "");
std::string capture(const std::vector<std::string>& args, bool quietErr = false);
