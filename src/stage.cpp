#include "stage.hpp"
#include "lang.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace fs = std::filesystem;

namespace {

std::string pngSizeDir(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char h[24] = {};
    in.read(reinterpret_cast<char*>(h), sizeof(h));
    if (in.gcount() < 24 || h[1] != 'P' || h[2] != 'N' || h[3] != 'G') return "256x256";
    uint32_t w = (uint32_t)h[16] << 24 | h[17] << 16 | h[18] << 8 | h[19];
    uint32_t ht = (uint32_t)h[20] << 24 | h[21] << 16 | h[22] << 8 | h[23];
    if (w != ht || w == 0 || w > 1024) return "256x256";
    return std::to_string(w) + "x" + std::to_string(w);
}

const auto exec755 = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                     fs::perms::others_read | fs::perms::others_exec;
const auto file644 = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                     fs::perms::others_read;

void writeFile(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << data;
    out.close();
    if (!out) throw std::runtime_error(lang::t("cant_write") + p.string());
}

}

std::string desktopPath(const Config& c) {
    return c.gui ? "usr/share/applications/" + c.name + ".desktop" : "";
}

std::string iconPath(const Config& c) {
    if (c.icon.empty()) return "";
    if (fs::path(c.icon).extension() == ".svg")
        return "usr/share/icons/hicolor/scalable/apps/" + c.name + ".svg";
    return "usr/share/icons/hicolor/" + pngSizeDir(c.icon) + "/apps/" + c.name + ".png";
}

void makeStage(const Config& c, const std::string& bin, const std::string& root) {
    if (!fs::is_regular_file(bin)) throw std::runtime_error(lang::t("binary_not_found") + bin);

    fs::remove_all(root);
    fs::create_directories(fs::path(root) / "usr/bin");
    auto dst = fs::path(root) / "usr/bin" / c.binary;
    fs::copy_file(bin, dst);
    fs::permissions(dst, exec755);

    if (!c.gui) return;

    if (auto icon = iconPath(c); !icon.empty()) {
        auto p = fs::path(root) / icon;
        fs::create_directories(p.parent_path());
        fs::copy_file(c.icon, p);
        fs::permissions(p, file644);
    }

    auto desktop = fs::path(root) / desktopPath(c);
    fs::create_directories(desktop.parent_path());
    std::string d = "[Desktop Entry]\nType=Application\nName=" + c.name + "\nComment=" + c.description +
                    "\nExec=" + c.binary + "\nCategories=" + c.categories + "\nTerminal=false\n";
    if (!c.icon.empty()) d += "Icon=" + c.name + "\n";
    writeFile(desktop, d);
    fs::permissions(desktop, file644);
}
