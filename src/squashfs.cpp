#include "squashfs.hpp"
#include "lang.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <zlib.h>
namespace fs = std::filesystem;

namespace {

const uint32_t BLOCK = 131072;
const uint32_t META = 8192;
const uint64_t NONE = ~0ull;
const uint32_t RAW_DATA = 0x01000000;
const uint16_t RAW_META = 0x8000;

enum InodeType : uint16_t { Dir = 1, File = 2, Link = 3, LongDir = 8, LongFile = 9 };

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)x);
    v.push_back((uint8_t)(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}
void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back((uint8_t)(x >> (8 * i)));
}

bool deflateSmaller(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    uLongf cap = compressBound((uLong)n);
    out.resize(cap);
    if (compress2(out.data(), &cap, src, (uLong)n, Z_BEST_COMPRESSION) != Z_OK) return false;
    out.resize(cap);
    return cap < n;
}

struct Meta {
    std::vector<uint8_t> pending;
    std::vector<uint8_t> blocks;

    uint64_t ref() const { return (uint64_t)blocks.size() << 16 | pending.size(); }

    void append(const std::vector<uint8_t>& data) {
        for (uint8_t b : data) {
            pending.push_back(b);
            if (pending.size() == META) flush();
        }
    }

    void flush() {
        if (pending.empty()) return;
        std::vector<uint8_t> z;
        if (deflateSmaller(pending.data(), pending.size(), z)) {
            put16(blocks, (uint16_t)z.size());
            blocks.insert(blocks.end(), z.begin(), z.end());
        } else {
            put16(blocks, (uint16_t)(pending.size() | RAW_META));
            blocks.insert(blocks.end(), pending.begin(), pending.end());
        }
        pending.clear();
    }
};

struct Node {
    std::string name;
    fs::path disk;
    bool dir = false;
    std::string target;
    uint16_t mode = 0644;
    uint32_t ino = 0;
    uint64_t ref = 0;
    std::vector<Node> kids;
};

Node scan(const fs::path& p, const std::string& name) {
    Node n;
    n.name = name;
    n.disk = p;
    n.dir = true;
    n.mode = 0755;
    for (auto& e : fs::directory_iterator(p)) {
        auto st = fs::symlink_status(e.path());
        if (fs::is_directory(st)) {
            n.kids.push_back(scan(e.path(), e.path().filename().string()));
        } else if (fs::is_regular_file(st)) {
            Node f;
            f.name = e.path().filename().string();
            f.disk = e.path();
            f.mode = (st.permissions() & fs::perms::owner_exec) != fs::perms::none ? 0755 : 0644;
            n.kids.push_back(f);
        } else if (fs::is_symlink(st)) {
            Node l;
            l.name = e.path().filename().string();
            l.target = fs::read_symlink(e.path()).string();
            l.mode = 0777;
            n.kids.push_back(l);
        }
    }
    std::sort(n.kids.begin(), n.kids.end(), [](const Node& a, const Node& b) {
        return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                            [](char x, char y) { return (uint8_t)x < (uint8_t)y; });
    });
    return n;
}

void number(Node& n, uint32_t& next) {
    for (auto& k : n.kids) number(k, next);
    n.ino = next++;
}

void common(std::vector<uint8_t>& r, uint16_t type, uint16_t mode, uint32_t ino) {
    put16(r, type);
    put16(r, mode);
    put16(r, 0);
    put16(r, 0);
    put32(r, 0);
    put32(r, ino);
}

class Writer {
public:
    Writer(std::ofstream& out, const std::string& path) : out_(out), path_(path) {}

    uint64_t pos = 96;
    Meta inodes, dirs;

    void dir(Node& n, uint32_t parent) {
        for (auto& k : n.kids) {
            if (k.dir) dir(k, n.ino);
            else if (!k.target.empty()) link(k);
            else file(k);
        }

        uint64_t at = dirs.ref();
        std::vector<uint8_t> list;
        for (size_t i = 0; i < n.kids.size();) {
            uint32_t block = (uint32_t)(n.kids[i].ref >> 16);
            uint32_t base = n.kids[i].ino;
            size_t j = i;
            while (j < n.kids.size() && j - i < 256 && (uint32_t)(n.kids[j].ref >> 16) == block) {
                int64_t d = (int64_t)n.kids[j].ino - base;
                if (d < -32768 || d > 32767) break;
                ++j;
            }
            put32(list, (uint32_t)(j - i - 1));
            put32(list, block);
            put32(list, base);
            for (size_t k = i; k < j; ++k) {
                auto& c = n.kids[k];
                put16(list, (uint16_t)(c.ref & 0xFFFF));
                put16(list, (uint16_t)(int16_t)((int64_t)c.ino - base));
                put16(list, c.dir ? Dir : !c.target.empty() ? Link : File);
                put16(list, (uint16_t)(c.name.size() - 1));
                list.insert(list.end(), c.name.begin(), c.name.end());
            }
            i = j;
        }
        dirs.append(list);

        uint32_t subdirs = (uint32_t)std::count_if(n.kids.begin(), n.kids.end(), [](const Node& k) { return k.dir; });
        uint64_t size = list.size() + 3;
        n.ref = inodes.ref();

        std::vector<uint8_t> r;
        if (size <= 0xFFFF) {
            common(r, Dir, n.mode, n.ino);
            put32(r, (uint32_t)(at >> 16));
            put32(r, 2 + subdirs);
            put16(r, (uint16_t)size);
            put16(r, (uint16_t)(at & 0xFFFF));
            put32(r, parent);
        } else {
            common(r, LongDir, n.mode, n.ino);
            put32(r, 2 + subdirs);
            put32(r, (uint32_t)size);
            put32(r, (uint32_t)(at >> 16));
            put32(r, parent);
            put16(r, 0);
            put16(r, (uint16_t)(at & 0xFFFF));
            put32(r, 0xFFFFFFFF);
        }
        inodes.append(r);
    }

private:
    std::ofstream& out_;
    std::string path_;

    void emit(const void* p, size_t n) {
        out_.write(static_cast<const char*>(p), (std::streamsize)n);
        if (!out_) throw std::runtime_error(lang::t("cant_write") + path_);
        pos += n;
    }

    void file(Node& n) {
        uint64_t size = fs::file_size(n.disk);
        uint64_t start = pos;
        std::vector<uint32_t> sizes;

        std::ifstream in(n.disk, std::ios::binary);
        std::vector<uint8_t> buf(BLOCK), z;
        for (uint64_t left = size; left > 0;) {
            size_t chunk = (size_t)std::min<uint64_t>(left, BLOCK);
            in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)chunk);
            if ((size_t)in.gcount() != chunk) throw std::runtime_error("read: " + n.disk.string());
            if (deflateSmaller(buf.data(), chunk, z)) {
                emit(z.data(), z.size());
                sizes.push_back((uint32_t)z.size());
            } else {
                emit(buf.data(), chunk);
                sizes.push_back((uint32_t)chunk | RAW_DATA);
            }
            left -= chunk;
        }

        n.ref = inodes.ref();
        std::vector<uint8_t> r;
        if (start <= 0xFFFFFFFF && size <= 0xFFFFFFFF) {
            common(r, File, n.mode, n.ino);
            put32(r, (uint32_t)start);
            put32(r, 0xFFFFFFFF);
            put32(r, 0);
            put32(r, (uint32_t)size);
        } else {
            common(r, LongFile, n.mode, n.ino);
            put64(r, start);
            put64(r, size);
            put64(r, 0);
            put32(r, 1);
            put32(r, 0xFFFFFFFF);
            put32(r, 0);
            put32(r, 0xFFFFFFFF);
        }
        for (uint32_t s : sizes) put32(r, s);
        inodes.append(r);
    }

    void link(Node& n) {
        n.ref = inodes.ref();
        std::vector<uint8_t> r;
        common(r, Link, n.mode, n.ino);
        put32(r, 1);
        put32(r, (uint32_t)n.target.size());
        r.insert(r.end(), n.target.begin(), n.target.end());
        inodes.append(r);
    }
};

}

void writeSquashfs(const std::string& rootDir, const std::string& outPath) {
    Node root = scan(rootDir, "");
    uint32_t next = 1;
    number(root, next);
    uint32_t count = next - 1;

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error(lang::t("cant_write") + outPath);
    std::vector<char> blank(96, 0);
    out.write(blank.data(), (std::streamsize)blank.size());

    Writer w(out, outPath);
    w.dir(root, count + 1);
    w.inodes.flush();
    w.dirs.flush();

    auto emit = [&](const std::vector<uint8_t>& v) {
        out.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)v.size());
        w.pos += v.size();
    };

    uint64_t inodeTable = w.pos;
    emit(w.inodes.blocks);
    uint64_t dirTable = w.pos;
    emit(w.dirs.blocks);

    Meta ids;
    std::vector<uint8_t> id;
    put32(id, 0);
    ids.append(id);
    ids.flush();
    uint64_t idBlock = w.pos;
    emit(ids.blocks);
    uint64_t idTable = w.pos;
    std::vector<uint8_t> ptr;
    put64(ptr, idBlock);
    emit(ptr);

    uint64_t used = w.pos;
    std::vector<char> pad((size_t)(((used + 4095) & ~4095ull) - used), 0);
    out.write(pad.data(), (std::streamsize)pad.size());

    std::vector<uint8_t> sb;
    put32(sb, 0x73717368);
    put32(sb, count);
    put32(sb, 0);
    put32(sb, BLOCK);
    put32(sb, 0);
    put16(sb, 1);
    put16(sb, 17);
    put16(sb, 0x0010 | 0x0040);
    put16(sb, 1);
    put16(sb, 4);
    put16(sb, 0);
    put64(sb, root.ref);
    put64(sb, used);
    put64(sb, idTable);
    put64(sb, NONE);
    put64(sb, inodeTable);
    put64(sb, dirTable);
    put64(sb, NONE);
    put64(sb, NONE);

    out.seekp(0);
    out.write(reinterpret_cast<const char*>(sb.data()), (std::streamsize)sb.size());
    out.close();
    if (!out) throw std::runtime_error(lang::t("cant_write") + outPath);
}
