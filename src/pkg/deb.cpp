#include "deb.hpp"
#include "../archive.hpp"
#include "../util.hpp"
#include <cstdint>
#include <filesystem>
#include <stdexcept>
namespace fs = std::filesystem;

namespace {

uint64_t installedKb(const std::string& stage) {
    uint64_t bytes = 0;
    for (auto& e : fs::recursive_directory_iterator(stage))
        if (e.is_regular_file()) bytes += e.file_size();
    return (bytes + 1023) / 1024;
}

}

std::string packDeb(const Config& c, const std::string& stage, const std::string& work, const std::string& outDir) {
    resetDir(work + "/control");

    std::string ctrl = "Package: " + c.name + "\n"
                       "Version: " + c.version + "\n"
                       "Architecture: " + c.arch + "\n"
                       "Maintainer: " + c.maintainer + "\n"
                       "Installed-Size: " + std::to_string(installedKb(stage)) + "\n";
    if (!c.depends.empty()) {
        ctrl += "Depends: ";
        for (size_t i = 0; i < c.depends.size(); ++i) ctrl += (i ? ", " : "") + c.depends[i];
        ctrl += "\n";
    }
    ctrl += "Section: utils\nPriority: optional\n";
    if (!c.url.empty()) ctrl += "Homepage: " + c.url + "\n";
    ctrl += "Description: " + c.description + "\n";
    writeFile(work + "/control/control", ctrl);
    writeFile(work + "/debian-binary", "2.0\n");

    writeTarGz(work + "/control", work + "/control.tar.gz", "./");
    writeTarGz(stage, work + "/data.tar.gz", "./");

    fs::create_directories(outDir);
    auto out = outDir + "/" + c.name + "_" + c.version + "_" + c.arch + ".deb";
    writeAr(out + ".part", {{"debian-binary", work + "/debian-binary"},
                            {"control.tar.gz", work + "/control.tar.gz"},
                            {"data.tar.gz", work + "/data.tar.gz"}});
    fs::rename(out + ".part", out);
    return out;
}
