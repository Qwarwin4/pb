#pragma once
#include <string>
#include <vector>

struct Config {
    std::string dir;
    std::string name;
    std::string version = "0.1.0";
    std::string description;
    std::string maintainer = "unknown";
    std::string license = "unspecified";
    std::string url;
    std::string binary;
    std::string arch;
    bool gui = false;
    std::string icon;
    std::string categories = "Utility;";
    std::vector<std::string> depends;

    static Config load(const std::string& projectDir);
};
