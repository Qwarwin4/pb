#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

void writeTarGz(const std::string& root, const std::string& out, const std::string& prefix,
                const std::function<bool(const std::string&)>& skip = {});
void writeAr(const std::string& out, const std::vector<std::pair<std::string, std::string>>& members);
