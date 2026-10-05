#include "config.hpp"
#include "lang.hpp"
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
namespace fs = std::filesystem;

namespace {

struct Value {
    enum Kind { Bad, Str, List, Bool } kind = Bad;
    std::string str;
    std::vector<std::string> list;
    bool flag = false;
};

size_t skipWs(const std::string& s, size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    return i;
}

bool onlyCommentLeft(const std::string& s, size_t i) {
    i = skipWs(s, i);
    return i >= s.size() || s[i] == '#';
}

bool readString(const std::string& s, size_t& i, std::string& out) {
    char q = s[i++];
    while (i < s.size() && s[i] != q) {
        if (q == '"' && s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == '"' || c == '\\') out += c;
            else { out += '\\'; out += c; }
            ++i;
            continue;
        }
        out += s[i++];
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
}

Value parseValue(const std::string& s) {
    Value v;
    size_t i = skipWs(s, 0);
    if (i >= s.size()) return v;

    if (s[i] == '"' || s[i] == '\'') {
        if (readString(s, i, v.str) && onlyCommentLeft(s, i)) v.kind = Value::Str;
        return v;
    }

    if (s[i] == '[') {
        ++i;
        for (;;) {
            i = skipWs(s, i);
            if (i < s.size() && s[i] == '#') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return v;
            std::string item;
            if (!readString(s, i, item)) return v;
            v.list.push_back(item);
            i = skipWs(s, i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            return v;
        }
        if (onlyCommentLeft(s, i)) v.kind = Value::List;
        return v;
    }

    auto end = s.find('#', i);
    std::string word = s.substr(i, end == std::string::npos ? std::string::npos : end - i);
    while (!word.empty() && (word.back() == ' ' || word.back() == '\t' || word.back() == '\r')) word.pop_back();
    if (word == "true" || word == "false") {
        v.kind = Value::Bool;
        v.flag = word == "true";
    } else if (!word.empty() && word.find_first_of(" \t\"'") == std::string::npos) {
        v.kind = Value::Str;
        v.str = word;
    }
    return v;
}

bool unclosedList(const std::string& s) {
    bool open = false;
    char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (q) {
            if (q == '"' && c == '\\') ++i;
            else if (c == q) q = 0;
        } else if (c == '"' || c == '\'') q = c;
        else if (c == '#') {
            while (i < s.size() && s[i] != '\n') ++i;
        }
        else if (c == '[') open = true;
        else if (c == ']') open = false;
    }
    return open;
}

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

bool hasControl(const std::string& s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return true;
    return false;
}

std::string normArch(std::string a) {
    if (a == "x86_64") return "amd64";
    if (a == "aarch64") return "arm64";
    if (a == "i686") return "i386";
    if (a == "any" || a == "noarch") return "all";
    return a;
}

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw std::runtime_error(path + ": " + msg);
}

}

Config Config::load(const std::string& projectDir) {
    std::string path = projectDir + "/build.toml";
    std::ifstream f(path);
    if (!f) {
        if (errno == ENOENT || errno == ENOTDIR) throw std::runtime_error("no_config:" + path);
        throw std::runtime_error(path + ": " + std::strerror(errno));
    }

    Config c;
    c.dir = fs::absolute(projectDir).lexically_normal().string();
    if (c.dir.size() > 1 && c.dir.back() == '/') c.dir.pop_back();

    auto str = [&](const Value& v, int n) {
        if (v.kind != Value::Str) fail(path, lang::t("cfg_parse") + std::to_string(n));
        return v.str;
    };

    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        int startLine = n;
        auto t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '[') continue;

        auto eq = t.find('=');
        if (eq == std::string::npos) fail(path, lang::t("cfg_parse") + std::to_string(n));
        auto key = trim(t.substr(0, eq));
        std::string raw = t.substr(eq + 1);
        std::string more;
        while (unclosedList(raw) && std::getline(f, more)) {
            ++n;
            raw += "\n" + more;
        }

        Value v = parseValue(raw);
        if (v.kind == Value::Bad) fail(path, lang::t("cfg_parse") + std::to_string(startLine));

        if (key == "name") c.name = str(v, startLine);
        else if (key == "version") c.version = str(v, startLine);
        else if (key == "description") c.description = str(v, startLine);
        else if (key == "maintainer") c.maintainer = str(v, startLine);
        else if (key == "license") c.license = str(v, startLine);
        else if (key == "url") c.url = str(v, startLine);
        else if (key == "binary") c.binary = str(v, startLine);
        else if (key == "arch") c.arch = normArch(str(v, startLine));
        else if (key == "icon") c.icon = str(v, startLine);
        else if (key == "categories") c.categories = str(v, startLine);
        else if (key == "gui") {
            if (v.kind != Value::Bool) fail(path, lang::t("cfg_parse") + std::to_string(startLine));
            c.gui = v.flag;
        } else if (key == "depends") {
            if (v.kind == Value::Str) c.depends = {v.str};
            else if (v.kind == Value::List) c.depends = v.list;
            else fail(path, lang::t("cfg_parse") + std::to_string(startLine));
        } else {
            std::cerr << lang::t("warning") << path << ":" << startLine << ": "
                      << lang::t("cfg_unknown") << key << "\n";
        }
    }

    if (c.name.empty()) fail(path, lang::t("cfg_no_name"));
    if (!std::regex_match(c.name, std::regex("[a-z0-9][a-z0-9+.-]+")))
        fail(path, lang::t("cfg_name") + c.name);
    if (!std::regex_match(c.version, std::regex("[0-9][A-Za-z0-9.+~]*")))
        fail(path, lang::t("cfg_version") + c.version);

    if (c.binary.empty()) c.binary = c.name;
    if (!std::regex_match(c.binary, std::regex("[A-Za-z0-9._+-]+")) || c.binary == "." || c.binary == "..")
        fail(path, lang::t("cfg_binary") + c.binary);

    if (!c.arch.empty() && !std::regex_match(c.arch, std::regex("amd64|arm64|i386|armhf|riscv64|all")))
        fail(path, lang::t("cfg_arch") + c.arch);

    for (auto* s : {&c.description, &c.maintainer, &c.license, &c.url, &c.categories, &c.icon})
        if (hasControl(*s)) fail(path, lang::t("cfg_ctrl") + *s);
    for (auto& d : c.depends)
        if (d.empty() || hasControl(d) || d.find(',') != std::string::npos)
            fail(path, lang::t("cfg_dep") + d);

    if (c.description.empty()) c.description = c.name;
    if (!c.categories.empty() && c.categories.back() != ';') c.categories += ';';

    if (!c.icon.empty()) {
        if (!c.gui) {
            std::cerr << lang::t("warning") << lang::t("cfg_icon_cli") << "\n";
            c.icon.clear();
        } else {
            fs::path p = c.icon;
            if (p.is_relative()) p = fs::path(c.dir) / p;
            if (!fs::is_regular_file(p)) fail(path, lang::t("cfg_icon_missing") + p.string());
            auto ext = p.extension().string();
            if (ext != ".png" && ext != ".svg") fail(path, lang::t("cfg_icon_ext") + p.string());
            c.icon = p.string();
        }
    }
    return c;
}
