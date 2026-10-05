#include "rpm.hpp"
#include "../rpmformat.hpp"
#include <algorithm>
#include <filesystem>
namespace fs = std::filesystem;

std::string rpmArch(const std::string& arch) {
    if (arch == "amd64") return "x86_64";
    if (arch == "arm64") return "aarch64";
    if (arch == "i386") return "i686";
    if (arch == "armhf") return "armv7hl";
    if (arch == "all") return "noarch";
    return arch;
}

std::string packRpm(const Config& c, const std::string& stage, const std::string& work, const std::string& outDir) {
    std::vector<RpmFile> files;
    for (auto& e : fs::recursive_directory_iterator(stage)) {
        if (!e.is_regular_file()) continue;
        bool exec = (e.status().permissions() & fs::perms::owner_exec) != fs::perms::none;
        files.push_back({"/" + fs::relative(e.path(), stage).generic_string(), e.path().string(),
                         exec ? 0755u : 0644u, e.file_size()});
    }
    std::sort(files.begin(), files.end(), [](const RpmFile& a, const RpmFile& b) { return a.path < b.path; });

    auto arch = rpmArch(c.arch);
    fs::create_directories(outDir);
    auto out = outDir + "/" + c.name + "-" + c.version + "-1." + arch + ".rpm";
    writeRpm(out, work, {c.name, c.version, "1", c.description, c.license, c.url, arch}, files);
    return out;
}
