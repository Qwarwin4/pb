#include "commands.hpp"
#include "config.hpp"
#include "build.hpp"
#include "elf.hpp"
#include "util.hpp"
#include "lang.hpp"
#include "proc.hpp"
#include "version.hpp"
#include "pkg/appimage.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <zlib.h>
namespace fs = std::filesystem;

namespace {

size_t width(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

void row(const std::string& k, const std::string& v, int indent = 2) {
    std::string pad(indent, ' ');
    size_t w = width(k);
    std::string out = v;
    for (size_t i = 0; (i = out.find('\n', i)) != std::string::npos; i += 1 + 16 + indent)
        out.insert(i + 1, std::string(16 + indent, ' '));
    std::cout << pad << k << std::string(w < 16 ? 16 - w : 1, ' ') << out << "\n";
}

void head(const std::string& s) { std::cout << "\n" << s << "\n"; }

std::string firstLine(const std::string& s) {
    auto p = s.find('\n');
    std::string l = s.substr(0, p);
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    return l;
}

std::string tryCapture(const std::vector<std::string>& args) {
    try {
        auto l = firstLine(capture(args, true));
        if (l.size() > 48) {
            auto cut = l.rfind(' ', 48);
            l = l.substr(0, cut == std::string::npos ? 48 : cut);
        }
        return l;
    } catch (...) {
        return "";
    }
}

std::string human(uintmax_t b) {
    const char* u[] = {"B", "KiB", "MiB", "GiB"};
    double v = (double)b;
    int i = 0;
    while (v >= 1024 && i < 3) {
        v /= 1024;
        ++i;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), i ? "%.1f %s" : "%.0f %s", v, u[i]);
    return buf;
}

uintmax_t dirSize(const std::string& p) {
    uintmax_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) n += it->file_size(ec);
    return n;
}

std::string osName() {
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        auto v = line.substr(12);
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'')) v = v.substr(1, v.size() - 2);
        return v;
    }
    return "?";
}

std::string hostGlibc() {
    auto g = tryCapture({"getconf", "GNU_LIBC_VERSION"});
    if (g.rfind("glibc ", 0) == 0) return g.substr(6);
    return "";
}

std::string binSummary(const BinInfo& b) {
    if (b.script) return "script";
    if (!b.elf) return lang::t("dbg_not_elf");
    std::string s = "ELF " + b.arch;
    s += b.dynamic ? ", glibc " + (b.glibc.empty() ? std::string("?") : b.glibc) : ", static";
    return s;
}

std::vector<std::string> binProblems(const BinInfo& b, const std::string& host, const std::string& glibc) {
    std::vector<std::string> out;
    if (b.elf && !b.arch.empty() && b.arch != host) out.push_back(lang::t("dbg_arch") + b.arch + " / " + host);
    if (b.elf && b.dynamic && !b.glibc.empty() && !glibc.empty() && versionParts(b.glibc) > versionParts(glibc))
        out.push_back(lang::t("dbg_glibc") + b.glibc + " > " + glibc);
    if (b.elf && b.dynamic && glibc.empty()) out.push_back(lang::t("dbg_no_glibc"));
    return out;
}

void environment() {
    auto self = selfPath();
    auto me = inspectBinary(self);
    auto host = hostArch();
    auto glibc = hostGlibc();

    head("pb");
    row(lang::t("dbg_version"), PB_VERSION);
    row(lang::t("dbg_path"), self + " (" + binSummary(me) + ")");
    row(lang::t("dbg_lang"), lang::get() + "  " + lang::file());
    auto cache = cacheDir();
    row(lang::t("dbg_cache"), cache + (fs::exists(cache) ? "  " + human(dirSize(cache)) : "  —"));
    for (auto a : {"amd64", "arm64", "i386", "armhf"}) {
        auto r = cache + "/runtime-" + appimageArch(a);
        if (fs::exists(r)) row("runtime", appimageArch(a) + (validRuntime(r, a) ? "  ok" : "  " + lang::t("dbg_broken")));
    }

    head(lang::t("dbg_system"));
    row(lang::t("dbg_os"), osName());
    row(lang::t("dbg_arch_host"), host);
    row("libc", glibc.empty() ? lang::t("dbg_no_glibc_host") : "glibc " + glibc);
    row("/usr/local/bin", access("/usr/local/bin", W_OK) == 0 ? lang::t("dbg_writable") : lang::t("dbg_root_only"));
    std::string fuse = fs::exists("/dev/fuse") ? "/dev/fuse" : lang::t("dbg_no_dev_fuse");
    for (auto f : {"fusermount3", "fusermount"})
        if (exists(f)) {
            fuse += ", " + std::string(f);
            break;
        }
    row("fuse", fuse);

    head(lang::t("dbg_tools"));
    std::vector<std::pair<std::string, std::vector<std::string>>> tools = {
        {"curl", {"curl", "--version"}},     {"wget", {"wget", "--version"}},
        {"make", {"make", "--version"}},     {"g++", {"g++", "--version"}},
        {"cmake", {"cmake", "--version"}},   {"cargo", {"cargo", "--version"}},
        {"go", {"go", "version"}},           {"git", {"git", "--version"}},
        {"dpkg", {"dpkg", "--version"}},     {"rpm", {"rpm", "--version"}},
        {"pacman", {"pacman", "--version"}}, {"makepkg", {"makepkg", "--version"}},
    };
    for (auto& [name, args] : tools) {
        auto p = findInPath(name);
        if (p.empty()) {
            row(name, "—");
            continue;
        }
        auto v = name == "pacman" ? "" : tryCapture(args);
        row(name, p + (v.empty() ? "" : "  " + v));
    }
}

void project(const std::string& dir) {
    head(lang::t("dbg_project") + fs::absolute(dir).lexically_normal().string());
    Config c;
    try {
        c = Config::load(dir);
    } catch (const std::exception& e) {
        std::string m = e.what();
        if (m.rfind("no_config:", 0) == 0) m = lang::t("no_config") + m.substr(10);
        row("build.toml", m);
        if (auto up = projRootAbove(dir); !up.empty()) row("", lang::t("proj_above") + up);
        return;
    }
    row("build.toml", c.name + " " + c.version);
    row("binary", c.binary);
    if (!c.arch.empty()) row("arch", c.arch);
    if (!c.depends.empty()) {
        std::string d;
        for (auto& x : c.depends) d += (d.empty() ? "" : ", ") + x;
        row("depends", d);
    }
    Proj p = detectProj(c.dir);
    row(lang::t("dbg_type"), projName(p));
    if (p == Proj::Unknown) {
        if (auto up = projRootAbove(c.dir); !up.empty()) row("", lang::t("proj_above") + up);
        else row("", lang::t("unknown_lang"));
        return;
    }
    if (p == Proj::Go) {
        try {
            row("main", goMain(c));
        } catch (const std::exception& e) {
            row("main", e.what());
        }
    }
    std::string tool = p == Proj::Cmake ? "cmake" : p == Proj::Cargo ? "cargo" : p == Proj::Go ? "go" : "make";
    if (!exists(tool)) row("", lang::t("not_found") + tool);
}

uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const unsigned char* p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
uint64_t le64(const unsigned char* p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

struct Found {
    std::string path;
    BinInfo info;
};

class GzIn {
public:
    GzIn(const std::string& path, uint64_t off) {
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error(path + ": " + std::strerror(errno));
        if (lseek(fd, (off_t)off, SEEK_SET) < 0) {
            close(fd);
            throw std::runtime_error(path + ": seek");
        }
        f_ = gzdopen(fd, "rb");
        if (!f_) {
            close(fd);
            throw std::runtime_error(path + ": gzip");
        }
    }
    ~GzIn() { gzclose(f_); }
    GzIn(const GzIn&) = delete;
    GzIn& operator=(const GzIn&) = delete;

    bool read(void* p, size_t n) {
        auto* c = static_cast<char*>(p);
        while (n) {
            int got = gzread(f_, c, (unsigned)std::min<size_t>(n, 1 << 20));
            if (got <= 0) return false;
            c += got;
            n -= (size_t)got;
        }
        return true;
    }
    bool skip(uint64_t n) {
        char buf[65536];
        while (n) {
            size_t k = (size_t)std::min<uint64_t>(n, sizeof(buf));
            if (!read(buf, k)) return false;
            n -= k;
        }
        return true;
    }
    bool copyTo(const std::string& out, uint64_t n) {
        std::ofstream o(out, std::ios::binary);
        char buf[65536];
        while (n) {
            size_t k = (size_t)std::min<uint64_t>(n, sizeof(buf));
            if (!read(buf, k)) return false;
            o.write(buf, (std::streamsize)k);
            n -= k;
        }
        return (bool)o;
    }

private:
    gzFile f_;
};

uint64_t octal(const char* p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n && p[i]; ++i)
        if (p[i] >= '0' && p[i] <= '7') v = v * 8 + (uint64_t)(p[i] - '0');
    return v;
}

void walkTar(const std::string& file, uint64_t off, const std::string& tmp, std::vector<std::string>& files,
             std::vector<Found>& bins, std::string* control) {
    GzIn gz(file, off);
    char h[512];
    std::string longName;
    while (gz.read(h, 512)) {
        if (h[0] == 0) break;
        uint64_t size = octal(h + 124, 12);
        char type = h[156];
        bool hadLong = !longName.empty();
        std::string name = hadLong ? longName : std::string(h, strnlen(h, 100));
        longName.clear();
        if (!hadLong && std::memcmp(h + 257, "ustar", 5) == 0 && h[345])
            name = std::string(h + 345, strnlen(h + 345, 155)) + "/" + name;
        uint64_t padded = (size + 511) / 512 * 512;
        if (type == 'L') {
            std::string s(size, '\0');
            if (!gz.read(s.data(), size) || !gz.skip(padded - size)) break;
            longName = s.c_str();
            continue;
        }
        while (name.rfind("./", 0) == 0) name = name.substr(2);
        bool reg = type == '0' || type == '\0';
        unsigned mode = (unsigned)octal(h + 100, 8);
        if (reg && control && name == "control") {
            control->assign(size, '\0');
            if (!gz.read(control->data(), size) || !gz.skip(padded - size)) break;
            continue;
        }
        if (reg && !control) files.push_back("/" + name);
        if (reg && !control && (mode & 0111)) {
            auto out = tmp + "/bin" + std::to_string(bins.size());
            if (!gz.copyTo(out, size) || !gz.skip(padded - size)) break;
            bins.push_back({"/" + name, inspectBinary(out)});
            continue;
        }
        if (type == 'x' || type == 'g' || type == 'K') {
            if (!gz.skip(padded)) break;
            continue;
        }
        if (!gz.skip(padded)) break;
    }
}

std::string field(const std::string& control, const std::string& key) {
    std::stringstream ss(control);
    std::string l;
    while (std::getline(ss, l))
        if (l.rfind(key + ":", 0) == 0) {
            auto v = l.substr(key.size() + 1);
            if (!v.empty() && v[0] == ' ') v.erase(0, 1);
            return firstLine(v);
        }
    return "";
}

void deb(const std::string& path, const std::string& tmp, std::vector<std::string>& probs, std::vector<Found>& bins) {
    std::ifstream in(path, std::ios::binary);
    char magic[8];
    in.read(magic, 8);
    std::map<std::string, std::pair<uint64_t, uint64_t>> members;
    char h[60];
    while (in.read(h, 60)) {
        std::string name(h, 16);
        name = name.substr(0, name.find_first_of(" /"));
        uint64_t size = std::strtoull(std::string(h + 48, 10).c_str(), nullptr, 10);
        uint64_t at = (uint64_t)in.tellg();
        members[name] = {at, size};
        in.seekg((std::streamoff)(at + size + (size & 1)));
    }
    std::string list;
    for (auto& [n, _] : members) list += (list.empty() ? "" : " ") + n;
    row(lang::t("dbg_members"), list);
    if (!members.count("debian-binary")) probs.push_back(lang::t("dbg_bad_deb"));

    std::string control;
    std::vector<std::string> files;
    if (members.count("control.tar.gz")) {
        walkTar(path, members["control.tar.gz"].first, tmp, files, bins, &control);
        row(lang::t("dbg_pkg"), field(control, "Package") + " " + field(control, "Version") + " " +
                                    field(control, "Architecture"));
        auto dep = field(control, "Depends");
        row("Depends", dep.empty() ? "—" : dep);
        auto a = field(control, "Architecture");
        if (!a.empty() && a != "all" && a != hostArch()) probs.push_back(lang::t("dbg_arch") + a + " / " + hostArch());
    } else {
        row(lang::t("dbg_pkg"), lang::t("dbg_cant_read"));
    }
    if (members.count("data.tar.gz")) {
        walkTar(path, members["data.tar.gz"].first, tmp, files, bins, nullptr);
        for (auto& f : files) row(lang::t("dbg_file"), f);
    } else {
        row(lang::t("dbg_file"), lang::t("dbg_cant_read"));
    }
}

void rpm(const std::string& path, const std::string& tmp, std::vector<std::string>& probs, std::vector<Found>& bins) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> all((std::istreambuf_iterator<char>(in)), {});
    auto header = [&](size_t at, size_t& end, std::map<uint32_t, std::vector<std::string>>* strs,
                      std::map<uint32_t, std::vector<uint32_t>>* ints) {
        if (at + 16 > all.size() || be32(&all[at]) != 0x8eade801) throw std::runtime_error(lang::t("dbg_bad_rpm"));
        uint32_t n = be32(&all[at + 8]), sz = be32(&all[at + 12]);
        size_t idx = at + 16, data = idx + 16 * (size_t)n;
        end = data + sz;
        if (end > all.size()) throw std::runtime_error(lang::t("dbg_bad_rpm"));
        for (uint32_t i = 0; strs && i < n; ++i) {
            const unsigned char* e = &all[idx + 16 * i];
            uint32_t tag = be32(e), type = be32(e + 4), off = be32(e + 8), cnt = be32(e + 12);
            size_t p = data + off;
            if (p >= end) continue;
            if (type == 6 || type == 8 || type == 9) {
                for (uint32_t k = 0; k < cnt && p < end; ++k) {
                    std::string s(reinterpret_cast<const char*>(&all[p]));
                    p += s.size() + 1;
                    (*strs)[tag].push_back(s);
                }
            } else if (type == 4) {
                for (uint32_t k = 0; k < cnt && p + 4 <= end; ++k, p += 4) (*ints)[tag].push_back(be32(&all[p]));
            }
        }
    };
    if (all.size() < 96 || be32(all.data()) != 0xedabeedb) throw std::runtime_error(lang::t("dbg_bad_rpm"));
    size_t end = 0;
    header(96, end, nullptr, nullptr);
    size_t main = (end + 7) / 8 * 8;
    std::map<uint32_t, std::vector<std::string>> s;
    std::map<uint32_t, std::vector<uint32_t>> v;
    header(main, end, &s, &v);
    auto one = [&](uint32_t t) { return s.count(t) && !s[t].empty() ? s[t][0] : std::string("?"); };
    row(lang::t("dbg_pkg"), one(1000) + " " + one(1001) + "-" + one(1002) + " " + one(1022));
    std::string req;
    for (auto& r : s[1049])
        if (r.rfind("rpmlib(", 0) != 0) req += (req.empty() ? "" : ", ") + r;
    row("Requires", req.empty() ? "—" : req);
    for (size_t i = 0; i < s[1117].size(); ++i) {
        uint32_t di = i < v[1116].size() ? v[1116][i] : 0;
        row(lang::t("dbg_file"), (di < s[1118].size() ? s[1118][di] : "") + s[1117][i]);
    }
    auto arch = one(1022);
    std::string want = hostArch() == "amd64" ? "x86_64" : hostArch() == "arm64" ? "aarch64" : hostArch();
    if (arch != "noarch" && arch != want && !(want == "i386" && arch.size() == 4 && arch[0] == 'i'))
        probs.push_back(lang::t("dbg_arch") + arch + " / " + want);
    auto comp = one(1125);
    row(lang::t("dbg_payload"), one(1124) + " / " + comp);
    if (comp != "gzip") return;

    GzIn gz(path, end);
    for (;;) {
        char h[110];
        if (!gz.read(h, 110) || std::memcmp(h, "070701", 6) != 0) break;
        auto hex = [&](int i) { return (uint64_t)std::strtoull(std::string(h + 6 + 8 * i, 8).c_str(), nullptr, 16); };
        uint64_t mode = hex(1), size = hex(6), nsz = hex(11);
        std::string name(nsz, '\0');
        if (!gz.read(name.data(), nsz)) break;
        name = name.c_str();
        gz.skip((4 - (110 + nsz) % 4) % 4);
        if (name == "TRAILER!!!") break;
        uint64_t pad = (4 - size % 4) % 4;
        if ((mode & 0170000) == 0100000 && (mode & 0111)) {
            auto out = tmp + "/bin" + std::to_string(bins.size());
            if (!gz.copyTo(out, size)) break;
            auto slash = name.find('/');
            bins.push_back({slash == std::string::npos ? "/" + name : name.substr(slash), inspectBinary(out)});
            gz.skip(pad);
        } else if (!gz.skip(size + pad)) {
            break;
        }
    }
}

void appimage(const std::string& path, std::vector<std::string>& probs) {
    std::ifstream in(path, std::ios::binary);
    unsigned char h[64] = {};
    in.read(reinterpret_cast<char*>(h), 64);
    bool is64 = h[4] == 2;
    uint64_t shoff = is64 ? le64(h + 0x28) : le32(h + 0x20);
    uint16_t shsz = is64 ? le16(h + 0x3A) : le16(h + 0x2E), shn = is64 ? le16(h + 0x3C) : le16(h + 0x30);
    uint64_t off = shoff + (uint64_t)shsz * shn;
    auto rt = inspectBinary(path);
    row("runtime", "ELF " + rt.arch + (rt.dynamic ? ", dynamic" : ", static"));
    if (!rt.arch.empty() && rt.arch != hostArch()) probs.push_back(lang::t("dbg_arch") + rt.arch + " / " + hostArch());

    unsigned char sb[96] = {};
    in.seekg((std::streamoff)off);
    in.read(reinterpret_cast<char*>(sb), 96);
    if (!in || le32(sb) != 0x73717368) {
        row("squashfs", lang::t("dbg_no_squashfs") + std::to_string(off));
        probs.push_back(lang::t("dbg_no_squashfs") + std::to_string(off));
        return;
    }
    const char* comp[] = {"?", "gzip", "lzma", "lzo", "xz", "lz4", "zstd"};
    uint16_t c = le16(sb + 20);
    row("squashfs", std::string("offset ") + std::to_string(off) + ", " + (c < 7 ? comp[c] : "?") + ", " +
                        std::to_string(le32(sb + 4)) + " inodes, " + human(le64(sb + 40)));
    if (le16(sb + 28) != 4) probs.push_back("squashfs " + std::to_string(le16(sb + 28)) + ".x");
    if (!fs::exists("/dev/fuse") || (!exists("fusermount3") && !exists("fusermount")))
        row("", lang::t("dbg_fuse_hint") + path + " --appimage-extract-and-run");
}

int checkFile(const std::string& path) {
    head(lang::t("dbg_check") + path);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << lang::t("error") << lang::t("binary_not_found") << path << "\n";
        return 1;
    }
    unsigned char m[12] = {};
    in.read(reinterpret_cast<char*>(m), 12);
    in.close();
    std::string kind = std::memcmp(m, "!<arch>\n", 8) == 0 ? "deb"
                       : be32(m) == 0xedabeedb                ? "rpm"
                       : std::memcmp(m, "\x7f" "ELF", 4) == 0
                           ? (m[8] == 'A' && m[9] == 'I' && m[10] == 2 ? "AppImage" : "ELF")
                       : m[0] == '#' && m[1] == '!' ? "script"
                                                    : "?";
    row(lang::t("dbg_kind"), kind + ", " + human(fs::file_size(path)));

    auto host = hostArch();
    auto glibc = hostGlibc();
    std::vector<std::string> probs;
    std::vector<Found> bins;
    TempDir tmp(cacheDir(), "debug");
    try {
        if (kind == "deb") deb(path, tmp.path(), probs, bins);
        else if (kind == "rpm") rpm(path, tmp.path(), probs, bins);
        else if (kind == "AppImage") appimage(path, probs);
        else if (kind == "ELF" || kind == "script") bins.push_back({path, inspectBinary(path)});
        else probs.push_back(lang::t("dbg_unknown_file"));
    } catch (const std::exception& e) {
        probs.push_back(e.what());
    }

    bool archBad = !probs.empty() && probs.front().rfind(lang::t("dbg_arch"), 0) == 0;
    for (auto& b : bins) {
        row(lang::t("dbg_binary"), b.path + "  (" + binSummary(b.info) + ")");
        for (auto& p : binProblems(b.info, host, glibc))
            if (!archBad || p.rfind(lang::t("dbg_arch"), 0) != 0) probs.push_back(b.path + ": " + p);
    }
    row(lang::t("dbg_host"), host + (glibc.empty() ? "" : ", glibc " + glibc));

    if (probs.empty()) {
        std::cout << "\n" << lang::t("dbg_ok") << "\n";
        return 0;
    }
    std::cout << "\n" << lang::t("dbg_problems") << "\n";
    for (auto& p : probs) std::cout << "  - " << p << "\n";
    return 1;
}

}

int cmdDebug(const std::string& target) {
    try {
        if (!target.empty() && fs::is_regular_file(target)) return checkFile(target);
        environment();
        project(target.empty() ? "." : target);
        std::cout << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << lang::t("error") << e.what() << "\n";
        return 1;
    }
}
