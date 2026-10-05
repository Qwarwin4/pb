#include "archive.hpp"
#include "lang.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <zlib.h>
namespace fs = std::filesystem;

namespace {

class Gz {
public:
    explicit Gz(const std::string& path) : path_(path), f_(gzopen(path.c_str(), "wb6")) {
        if (!f_) throw std::runtime_error(lang::t("cant_write") + path);
    }
    ~Gz() {
        if (f_) gzclose(f_);
    }
    void write(const void* p, size_t n) {
        if (n && gzwrite(f_, p, (unsigned)n) != (int)n) throw std::runtime_error(lang::t("cant_write") + path_);
    }
    void close() {
        int rc = gzclose(f_);
        f_ = nullptr;
        if (rc != Z_OK) throw std::runtime_error(lang::t("cant_write") + path_);
    }

private:
    std::string path_;
    gzFile f_;
};

void splitName(const std::string& full, std::string& name, std::string& prefix) {
    if (full.size() <= 100) {
        name = full;
        prefix.clear();
        return;
    }
    for (size_t i = full.size() - 1; i > 0; --i) {
        if (full[i] != '/' || i == full.size() - 1) continue;
        if (i <= 155 && full.size() - i - 1 <= 100) {
            prefix = full.substr(0, i);
            name = full.substr(i + 1);
            return;
        }
    }
    throw std::runtime_error("tar: path too long: " + full);
}

void header(Gz& gz, const std::string& path, uint64_t size, unsigned mode, char type,
            const std::string& link = "") {
    char h[512] = {};
    std::string name, prefix;
    splitName(path, name, prefix);
    if (link.size() > 100) throw std::runtime_error("tar: link target too long: " + link);

    std::memcpy(h, name.data(), name.size());
    std::snprintf(h + 100, 8, "%07o", mode & 07777);
    std::snprintf(h + 108, 8, "%07o", 0);
    std::snprintf(h + 116, 8, "%07o", 0);
    std::snprintf(h + 124, 12, "%011llo", (unsigned long long)size);
    std::snprintf(h + 136, 12, "%011o", 0);
    std::memset(h + 148, ' ', 8);
    h[156] = type;
    std::memcpy(h + 157, link.data(), link.size());
    std::memcpy(h + 257, "ustar", 6);
    h[263] = '0';
    h[264] = '0';
    std::memcpy(h + 265, "root", 4);
    std::memcpy(h + 297, "root", 4);
    std::memcpy(h + 345, prefix.data(), prefix.size());

    unsigned sum = 0;
    for (unsigned char c : h) sum += c;
    std::snprintf(h + 148, 8, "%06o", sum);
    h[155] = ' ';
    gz.write(h, sizeof(h));
}

void fileBody(Gz& gz, const fs::path& p, uint64_t size) {
    std::ifstream in(p, std::ios::binary);
    std::vector<char> buf(1 << 16);
    uint64_t left = size;
    while (left > 0) {
        size_t chunk = (size_t)std::min<uint64_t>(left, buf.size());
        in.read(buf.data(), (std::streamsize)chunk);
        if ((size_t)in.gcount() != chunk) throw std::runtime_error("read: " + p.string());
        gz.write(buf.data(), chunk);
        left -= chunk;
    }
    static const char zeros[512] = {};
    gz.write(zeros, (512 - size % 512) % 512);
}

}

void writeTarGz(const std::string& root, const std::string& out, const std::string& prefix,
                const std::vector<std::string>& skip) {
    std::vector<fs::path> entries;
    fs::recursive_directory_iterator it(root), end;
    for (; it != end; ++it) {
        auto rel = fs::relative(it->path(), root).generic_string();
        if (std::find(skip.begin(), skip.end(), rel) != skip.end()) {
            it.disable_recursion_pending();
            continue;
        }
        entries.push_back(it->path());
    }
    std::sort(entries.begin(), entries.end());

    Gz gz(out);
    header(gz, prefix, 0, 0755, '5');
    for (auto& p : entries) {
        auto name = prefix + fs::relative(p, root).generic_string();
        auto st = fs::symlink_status(p);
        if (fs::is_symlink(st)) {
            header(gz, name, 0, 0777, '2', fs::read_symlink(p).string());
        } else if (fs::is_directory(st)) {
            header(gz, name + "/", 0, 0755, '5');
        } else if (fs::is_regular_file(st)) {
            bool exec = (st.permissions() & fs::perms::owner_exec) != fs::perms::none;
            auto size = fs::file_size(p);
            header(gz, name, size, exec ? 0755 : 0644, '0');
            fileBody(gz, p, size);
        }
    }
    static const char trailer[1024] = {};
    gz.write(trailer, sizeof(trailer));
    gz.close();
}

void writeAr(const std::string& out, const std::vector<std::pair<std::string, std::string>>& members) {
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error(lang::t("cant_write") + out);
    f.write("!<arch>\n", 8);

    for (auto& [name, path] : members) {
        auto size = fs::file_size(path);
        char h[61];
        std::snprintf(h, sizeof(h), "%-16s%-12s%-6s%-6s%-8s%-10llu`\n",
                      name.c_str(), "0", "0", "0", "100644", (unsigned long long)size);
        f.write(h, 60);
        std::ifstream in(path, std::ios::binary);
        f << in.rdbuf();
        if (size % 2) f.put('\n');
    }
    f.close();
    if (!f) throw std::runtime_error(lang::t("cant_write") + out);
}
