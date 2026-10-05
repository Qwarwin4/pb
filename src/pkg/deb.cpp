#include "deb.hpp"
#include "../archive.hpp"
#include "../fsutil.hpp"
#include "../lang.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    auto dir = work + "/deb";
    resetDir(dir + "/control");

    std::ofstream ctrl(dir + "/control/control");
    ctrl << "Package: " << c.name << "\n"
         << "Version: " << c.version << "\n"
         << "Architecture: " << c.arch << "\n"
         << "Maintainer: " << c.maintainer << "\n"
         << "Installed-Size: " << installedKb(stage) << "\n";
    if (!c.depends.empty()) {
        ctrl << "Depends: ";
        for (size_t i = 0; i < c.depends.size(); ++i) ctrl << (i ? ", " : "") << c.depends[i];
        ctrl << "\n";
    }
    ctrl << "Section: utils\n"
         << "Priority: optional\n";
    if (!c.url.empty()) ctrl << "Homepage: " << c.url << "\n";
    ctrl << "Description: " << c.description << "\n";
    ctrl.close();
    if (!ctrl) throw std::runtime_error(lang::t("cant_write") + dir + "/control/control");

    std::ofstream db(dir + "/debian-binary");
    db << "2.0\n";
    db.close();
    if (!db) throw std::runtime_error(lang::t("cant_write") + dir + "/debian-binary");

    writeTarGz(dir + "/control", dir + "/control.tar.gz", "./");
    writeTarGz(stage, dir + "/data.tar.gz", "./");

    fs::create_directories(outDir);
    auto out = outDir + "/" + c.name + "_" + c.version + "_" + c.arch + ".deb";
    writeAr(out, {{"debian-binary", dir + "/debian-binary"},
                  {"control.tar.gz", dir + "/control.tar.gz"},
                  {"data.tar.gz", dir + "/data.tar.gz"}});
    return out;
}
