#include "rpmformat.hpp"
#include "lang.hpp"
#include "md5.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <zlib.h>
namespace fs = std::filesystem;

namespace {

enum Type { Int16 = 3, Int32 = 4, Str = 6, Bin = 7, StrArray = 8, I18n = 9 };

const int32_t SENSE_EQUAL = 8;
const int32_t SENSE_RPMLIB = (1 << 24) | 2 | 8;
const int32_t DIGEST_SHA256 = 8;

void be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

class Header {
public:
    void str(int32_t tag, const std::string& s, Type t = Str) {
        std::vector<uint8_t> d(s.begin(), s.end());
        d.push_back(0);
        add(tag, t, d, 1);
    }
    void strs(int32_t tag, const std::vector<std::string>& v) {
        std::vector<uint8_t> d;
        for (auto& s : v) {
            d.insert(d.end(), s.begin(), s.end());
            d.push_back(0);
        }
        add(tag, StrArray, d, (int32_t)v.size());
    }
    void i32(int32_t tag, const std::vector<int32_t>& v) {
        std::vector<uint8_t> d(v.size() * 4);
        for (size_t i = 0; i < v.size(); ++i) be32(&d[i * 4], (uint32_t)v[i]);
        add(tag, Int32, d, (int32_t)v.size());
    }
    void i16(int32_t tag, const std::vector<uint16_t>& v) {
        std::vector<uint8_t> d(v.size() * 2);
        for (size_t i = 0; i < v.size(); ++i) {
            d[i * 2] = (uint8_t)(v[i] >> 8);
            d[i * 2 + 1] = (uint8_t)v[i];
        }
        add(tag, Int16, d, (int32_t)v.size());
    }
    void bin(int32_t tag, const uint8_t* p, size_t n) {
        add(tag, Bin, std::vector<uint8_t>(p, p + n), (int32_t)n);
    }

    std::vector<uint8_t> build(int32_t regionTag) {
        std::sort(entries_.begin(), entries_.end(), [](const E& a, const E& b) { return a.tag < b.tag; });
        int32_t count = (int32_t)entries_.size() + 1;

        std::vector<uint8_t> store;
        std::vector<int32_t> offsets;
        for (auto& e : entries_) {
            size_t align = e.type == Int32 ? 4 : e.type == Int16 ? 2 : 1;
            while (store.size() % align) store.push_back(0);
            offsets.push_back((int32_t)store.size());
            store.insert(store.end(), e.data.begin(), e.data.end());
        }

        uint8_t region[16];
        be32(region, (uint32_t)regionTag);
        be32(region + 4, Bin);
        be32(region + 8, (uint32_t)(-(count * 16)));
        be32(region + 12, 16);
        int32_t regionOff = (int32_t)store.size();
        store.insert(store.end(), region, region + 16);

        std::vector<uint8_t> out = {0x8e, 0xad, 0xe8, 0x01, 0, 0, 0, 0};
        out.resize(16);
        be32(&out[8], (uint32_t)count);
        be32(&out[12], (uint32_t)store.size());

        auto index = [&](int32_t tag, int32_t type, int32_t off, int32_t n) {
            uint8_t b[16];
            be32(b, (uint32_t)tag);
            be32(b + 4, (uint32_t)type);
            be32(b + 8, (uint32_t)off);
            be32(b + 12, (uint32_t)n);
            out.insert(out.end(), b, b + 16);
        };
        index(regionTag, Bin, regionOff, 16);
        for (size_t i = 0; i < entries_.size(); ++i)
            index(entries_[i].tag, entries_[i].type, offsets[i], entries_[i].count);
        out.insert(out.end(), store.begin(), store.end());
        return out;
    }

private:
    struct E {
        int32_t tag;
        int32_t type;
        std::vector<uint8_t> data;
        int32_t count;
    };
    std::vector<E> entries_;

    void add(int32_t tag, Type t, std::vector<uint8_t> d, int32_t n) {
        entries_.push_back({tag, t, std::move(d), n});
    }
};

class Cpio {
public:
    explicit Cpio(const std::string& path) : path_(path), gz_(gzopen(path.c_str(), "wb9")) {
        if (!gz_) throw std::runtime_error(lang::t("cant_write") + path);
    }
    ~Cpio() {
        if (gz_) gzclose(gz_);
    }

    void entry(uint32_t ino, uint32_t mode, uint32_t mtime, uint64_t size, const std::string& name,
               const std::string& disk) {
        char h[111];
        uint32_t namesize = (uint32_t)name.size() + 1;
        std::snprintf(h, sizeof(h), "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
                      ino, mode, 0u, 0u, 1u, mtime, (uint32_t)size, 0u, 0u, 0u, 0u, namesize, 0u);
        put(h, 110);
        put(name.c_str(), namesize);
        pad(110 + namesize);

        if (size == 0) return;
        std::ifstream in(disk, std::ios::binary);
        std::vector<char> buf(1 << 16);
        uint64_t left = size;
        while (left > 0) {
            size_t chunk = (size_t)std::min<uint64_t>(left, buf.size());
            in.read(buf.data(), (std::streamsize)chunk);
            if ((size_t)in.gcount() != chunk) throw std::runtime_error("read: " + disk);
            put(buf.data(), chunk);
            left -= chunk;
        }
        pad(size);
    }

    uint64_t close() {
        entry(0, 0, 0, 0, "TRAILER!!!", "");
        int rc = gzclose(gz_);
        gz_ = nullptr;
        if (rc != Z_OK) throw std::runtime_error(lang::t("cant_write") + path_);
        return raw_;
    }

private:
    std::string path_;
    gzFile gz_;
    uint64_t raw_ = 0;

    void put(const void* p, size_t n) {
        if (n && gzwrite(gz_, p, (unsigned)n) != (int)n) throw std::runtime_error(lang::t("cant_write") + path_);
        raw_ += n;
    }
    void pad(uint64_t n) {
        static const char z[4] = {};
        put(z, (4 - n % 4) % 4);
    }
};

struct Removal {
    std::string path;
    ~Removal() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

uint32_t buildTime() {
    if (const char* s = std::getenv("SOURCE_DATE_EPOCH"); s && *s) return (uint32_t)std::strtoul(s, nullptr, 10);
    return (uint32_t)std::time(nullptr);
}

}

void writeRpm(const std::string& out, const std::string& work, const RpmMeta& m, const std::vector<RpmFile>& files) {
    uint32_t mtime = buildTime();
    std::vector<std::string> dirnames, basenames, digests, empty, root;
    std::vector<int32_t> dirIndex, sizes, mtimes, flags, devices, inodes;
    std::vector<uint16_t> modes, rdevs;
    std::map<std::string, int32_t> dirSeen;
    uint64_t total = 0;

    for (size_t i = 0; i < files.size(); ++i) {
        auto& f = files[i];
        if (f.size > 0xFFFFFFFFull) throw std::runtime_error("rpm: file larger than 4 GiB: " + f.path);
        auto slash = f.path.rfind('/');
        auto dir = f.path.substr(0, slash + 1);
        auto it = dirSeen.find(dir);
        if (it == dirSeen.end()) {
            it = dirSeen.emplace(dir, (int32_t)dirnames.size()).first;
            dirnames.push_back(dir);
        }
        dirIndex.push_back(it->second);
        basenames.push_back(f.path.substr(slash + 1));
        sizes.push_back((int32_t)f.size);
        modes.push_back((uint16_t)(0100000 | f.mode));
        rdevs.push_back(0);
        mtimes.push_back((int32_t)mtime);
        flags.push_back(0);
        devices.push_back(1);
        inodes.push_back((int32_t)i + 1);
        digests.push_back(sha256File(f.disk));
        empty.push_back("");
        root.push_back("root");
        total += f.size;
    }

    Removal payload{work + "/payload.cpio.gz"};
    Cpio cpio(payload.path);
    for (size_t i = 0; i < files.size(); ++i)
        cpio.entry((uint32_t)i + 1, 0100000 | files[i].mode, mtime, files[i].size, "." + files[i].path, files[i].disk);
    uint64_t rawSize = cpio.close();
    uint64_t payloadSize = fs::file_size(payload.path);
    if (rawSize > 0xFFFFFFFFull || payloadSize > 0xFFFFFFFFull)
        throw std::runtime_error("rpm: payload larger than 4 GiB");

    Header h;
    h.strs(100, {"C"});
    h.str(1000, m.name);
    h.str(1001, m.version);
    h.str(1002, m.release);
    h.str(1004, m.summary, I18n);
    h.str(1005, m.summary, I18n);
    h.i32(1006, {(int32_t)mtime});
    h.str(1007, "pb");
    h.i32(1009, {(int32_t)total});
    h.str(1014, m.license);
    h.str(1016, "Unspecified", I18n);
    if (!m.url.empty()) h.str(1020, m.url);
    h.str(1021, "linux");
    h.str(1022, m.arch);
    h.i32(1028, sizes);
    h.i16(1030, modes);
    h.i16(1033, rdevs);
    h.i32(1034, mtimes);
    h.strs(1035, digests);
    h.strs(1036, empty);
    h.i32(1037, flags);
    h.strs(1039, root);
    h.strs(1040, root);
    h.strs(1047, {m.name});
    h.i32(1048, {SENSE_RPMLIB, SENSE_RPMLIB, SENSE_RPMLIB});
    h.strs(1049, {"rpmlib(CompressedFileNames)", "rpmlib(FileDigests)", "rpmlib(PayloadFilesHavePrefix)"});
    h.strs(1050, {"3.0.4-1", "4.6.0-1", "4.0-1"});
    h.i32(1095, devices);
    h.i32(1096, inodes);
    h.strs(1097, empty);
    h.i32(1112, {SENSE_EQUAL});
    h.strs(1113, {m.version + "-" + m.release});
    h.i32(1116, dirIndex);
    h.strs(1117, basenames);
    h.strs(1118, dirnames);
    h.str(1124, "cpio");
    h.str(1125, "gzip");
    h.str(1126, "9");
    h.i32(5011, {DIGEST_SHA256});
    h.strs(5092, {sha256File(payload.path)});
    h.i32(5093, {DIGEST_SHA256});
    auto main = h.build(63);

    Md5 md5;
    md5.update(main.data(), main.size());
    {
        std::ifstream in(payload.path, std::ios::binary);
        std::vector<char> buf(1 << 16);
        while (in) {
            in.read(buf.data(), (std::streamsize)buf.size());
            md5.update(buf.data(), (size_t)in.gcount());
        }
    }
    auto md5sum = md5.digest();

    Header sig;
    sig.str(273, sha256Hex(main.data(), main.size()));
    sig.i32(1000, {(int32_t)(main.size() + payloadSize)});
    sig.bin(1004, md5sum.data(), md5sum.size());
    sig.i32(1007, {(int32_t)rawSize});
    auto sigBytes = sig.build(62);

    uint8_t lead[96] = {};
    const uint8_t magic[] = {0xed, 0xab, 0xee, 0xdb, 3, 0, 0, 0, 0, 1};
    std::memcpy(lead, magic, sizeof(magic));
    auto nvr = m.name + "-" + m.version + "-" + m.release;
    std::memcpy(lead + 10, nvr.data(), std::min<size_t>(nvr.size(), 65));
    lead[77] = 1;
    lead[79] = 5;

    std::string tmp = out + ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(lead), sizeof(lead));
        f.write(reinterpret_cast<const char*>(sigBytes.data()), (std::streamsize)sigBytes.size());
        static const char zeros[8] = {};
        f.write(zeros, (std::streamsize)((8 - sigBytes.size() % 8) % 8));
        f.write(reinterpret_cast<const char*>(main.data()), (std::streamsize)main.size());
        std::ifstream in(payload.path, std::ios::binary);
        f << in.rdbuf();
        f.close();
        if (!f) {
            fs::remove(tmp);
            throw std::runtime_error(lang::t("cant_write") + out);
        }
    }
    fs::rename(tmp, out);
}
