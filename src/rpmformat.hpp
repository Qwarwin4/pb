#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RpmFile {
    std::string path;
    std::string disk;
    unsigned mode;
    uint64_t size;
};

struct RpmMeta {
    std::string name;
    std::string version;
    std::string release;
    std::string summary;
    std::string license;
    std::string url;
    std::string arch;
};

void writeRpm(const std::string& out, const std::string& work, const RpmMeta& m, const std::vector<RpmFile>& files);
