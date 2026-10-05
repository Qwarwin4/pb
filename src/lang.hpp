#pragma once
#include <string>

namespace lang {

std::string get();
bool set(const std::string& code);
std::string file();
std::string t(const std::string& key);

}
