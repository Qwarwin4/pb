#pragma once
#include <string>

struct BinInfo {
    bool elf = false;
    bool script = false;
    std::string arch;
    bool dynamic = false;
    std::string glibc;
    std::string glibcxx;
};

BinInfo inspectBinary(const std::string& path);
std::string hostArch();
std::string portabilityProblem(const BinInfo& b);
