#include "elf.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sys/utsname.h>
#include <vector>

namespace {

uint16_t rd16(const std::vector<char>& b, size_t o) {
    return (uint8_t)b[o] | (uint16_t)(uint8_t)b[o + 1] << 8;
}

uint32_t rd32(const std::vector<char>& b, size_t o) {
    return rd16(b, o) | (uint32_t)rd16(b, o + 2) << 16;
}

uint64_t rd64(const std::vector<char>& b, size_t o) {
    return rd32(b, o) | (uint64_t)rd32(b, o + 4) << 32;
}

std::vector<int> parseVer(const std::string& s) {
    std::vector<int> v;
    int cur = 0;
    bool any = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
        else if (c == '.' && any) { v.push_back(cur); cur = 0; any = false; }
        else break;
    }
    if (any) v.push_back(cur);
    return v;
}

std::string maxSymbolVersion(const std::vector<char>& b, const std::string& prefix) {
    std::string best;
    std::vector<int> bestV;
    size_t pos = 0;
    std::string hay(b.begin(), b.end());
    while ((pos = hay.find(prefix, pos)) != std::string::npos) {
        pos += prefix.size();
        size_t end = pos;
        while (end < hay.size() && ((hay[end] >= '0' && hay[end] <= '9') || hay[end] == '.')) ++end;
        if (end == pos) continue;
        std::string ver = hay.substr(pos, end - pos);
        auto v = parseVer(ver);
        if (v > bestV) {
            bestV = v;
            best = ver;
        }
    }
    return best;
}

std::string archFromMachine(uint16_t m, bool is64) {
    switch (m) {
        case 62: return "amd64";
        case 183: return "arm64";
        case 3: return "i386";
        case 40: return "armhf";
        case 243: return is64 ? "riscv64" : "";
        default: return "";
    }
}

}

BinInfo inspectBinary(const std::string& path) {
    BinInfo info;
    std::ifstream in(path, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() >= 2 && b[0] == '#' && b[1] == '!') {
        info.script = true;
        info.arch = "all";
        return info;
    }
    if (b.size() < 52 || std::memcmp(b.data(), "\x7f" "ELF", 4) != 0 || b[5] != 1) return info;

    bool is64 = b[4] == 2;
    if (is64 && b.size() < 64) return info;
    info.elf = true;
    info.arch = archFromMachine(rd16(b, 18), is64);

    uint64_t phoff = is64 ? rd64(b, 32) : rd32(b, 28);
    uint16_t phentsize = rd16(b, is64 ? 54 : 42);
    uint16_t phnum = rd16(b, is64 ? 56 : 44);
    for (uint16_t i = 0; i < phnum; ++i) {
        uint64_t o = phoff + (uint64_t)i * phentsize;
        if (o + 4 > b.size()) break;
        if (rd32(b, o) == 3) info.dynamic = true;
    }

    if (info.dynamic) {
        info.glibc = maxSymbolVersion(b, "GLIBC_");
        info.glibcxx = maxSymbolVersion(b, "GLIBCXX_");
    }
    return info;
}

std::string hostArch() {
    struct utsname u;
    if (uname(&u) != 0) return "";
    std::string m = u.machine;
    if (m == "x86_64") return "amd64";
    if (m == "aarch64" || m == "arm64") return "arm64";
    if (m == "i386" || m == "i486" || m == "i586" || m == "i686") return "i386";
    if (m.rfind("armv7", 0) == 0 || m.rfind("armv8l", 0) == 0) return "armhf";
    if (m == "riscv64") return "riscv64";
    return m;
}

std::string portabilityProblem(const BinInfo& b) {
    if (!b.dynamic) return "";
    std::string out;
    if (!b.glibc.empty() && parseVer(b.glibc) > parseVer("2.31")) out = "GLIBC_" + b.glibc;
    if (!b.glibcxx.empty() && parseVer(b.glibcxx) > parseVer("3.4.28"))
        out += (out.empty() ? "" : ", ") + std::string("GLIBCXX_") + b.glibcxx;
    return out;
}
