#include "build.hpp"
#include "commands.hpp"
#include "config.hpp"
#include "detect.hpp"
#include "elf.hpp"
#include "fsutil.hpp"
#include "lang.hpp"
#include "stage.hpp"
#include "version.hpp"
#include "pkg/appimage.hpp"
#include "pkg/aur.hpp"
#include "pkg/deb.hpp"
#include "pkg/rpm.hpp"
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <set>
#include <vector>
namespace fs = std::filesystem;

namespace {

void help() {
    if (lang::get() == "en") {
        std::cout <<
            "pb " << PB_VERSION << " -- builds .deb / .rpm / .AppImage / PKGBUILD from one config\n"
            "\n"
            "COMMANDS\n"
            "  build [DIR]              package the project in DIR (default: current dir)\n"
            "  install                  install pb to /usr/local/bin\n"
            "  uninstall [--purge]      remove pb (--purge also wipes config and cache)\n"
            "  repair                   check and fix the installation, config and cache\n"
            "  update [--yes]           update pb from GitHub Releases\n"
            "  lang ru|en               message language\n"
            "  --version                print version\n"
            "  -h, --help               this message\n"
            "\n"
            "BUILD FLAGS\n"
            "  --deb --rpm --appimage --aur   formats to build (default: all four)\n"
            "  --out DIR                output dir (default: ~/pb-builds/<format>/)\n"
            "  --binary PATH            package a prebuilt binary instead of building\n"
            "\n"
            "BUILD.TOML\n"
            "    name         = \"myapp\"\n"
            "    version      = \"1.2.0\"\n"
            "    description  = \"My app\"\n"
            "    maintainer   = \"Me <mail@example.com>\"\n"
            "    license      = \"GPL-3.0-or-later\"\n"
            "    url          = \"https://github.com/me/myapp\"\n"
            "    binary       = \"myapp\"\n"
            "    depends      = [\"libc6\"]\n"
            "\n"
            "  Only name is required. The architecture is read from the binary.\n"
            "  For GUI apps add gui = true, icon = \"icon.png\" and categories = \"Utility;\"\n"
            "  to get a .desktop entry and an icon.\n";
    } else {
        std::cout <<
            "pb " << PB_VERSION << " -- собирает .deb / .rpm / .AppImage / PKGBUILD из одного конфига\n"
            "\n"
            "КОМАНДЫ\n"
            "  build [DIR]              упаковать проект из DIR (по умолч. - текущая папка)\n"
            "  install                  установить pb в /usr/local/bin\n"
            "  uninstall [--purge]      удалить pb (--purge стирает и конфиг, и кэш)\n"
            "  repair                   проверить и починить установку, конфиг и кэш\n"
            "  update [--yes]           обновить pb из GitHub Releases\n"
            "  lang ru|en               язык сообщений\n"
            "  --version                версия\n"
            "  -h, --help               это сообщение\n"
            "\n"
            "ФЛАГИ build\n"
            "  --deb --rpm --appimage --aur   какие форматы собирать (по умолч. все четыре)\n"
            "  --out DIR                куда класть результат (по умолч. ~/pb-builds/<формат>/)\n"
            "  --binary PATH            упаковать готовый бинарник вместо сборки\n"
            "\n"
            "BUILD.TOML\n"
            "    name         = \"myapp\"\n"
            "    version      = \"1.2.0\"\n"
            "    description  = \"Моё приложение\"\n"
            "    maintainer   = \"Я <mail@example.com>\"\n"
            "    license      = \"GPL-3.0-or-later\"\n"
            "    url          = \"https://github.com/me/myapp\"\n"
            "    binary       = \"myapp\"\n"
            "    depends      = [\"libc6\"]\n"
            "\n"
            "  Обязательно только name. Архитектура определяется по бинарнику.\n"
            "  Для графических программ добавь gui = true, icon = \"icon.png\" и\n"
            "  categories = \"Utility;\" — будут .desktop-файл и иконка.\n";
    }
}

int fail(const std::string& msg) {
    std::cerr << lang::t("error") << msg << "\n";
    return 1;
}

int build(int argc, char** argv) {
    std::string dir = ".", outDir, binPath;
    std::set<std::string> formats;
    bool dirGiven = false;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        bool hasVal = i + 1 < argc && argv[i + 1][0] != '-';
        if (a == "--out" || a == "--binary") {
            if (!hasVal) return fail(a + lang::t("needs_value"));
            (a == "--out" ? outDir : binPath) = argv[++i];
        } else if (a == "--deb" || a == "--rpm" || a == "--appimage" || a == "--aur") {
            formats.insert(a.substr(2));
        } else if (!dirGiven && !a.empty() && a[0] != '-') {
            dir = a;
            dirGiven = true;
        } else {
            return fail(lang::t("unknown_arg") + a);
        }
    }
    if (formats.empty()) formats = {"deb", "rpm", "appimage", "aur"};

    try {
        Config c = Config::load(dir);
        std::cout << lang::t("pkg_info") << c.name << " " << c.version << std::endl;

        Proj proj = detectProj(c.dir);
        if (!binPath.empty()) {
            binPath = fs::absolute(binPath).lexically_normal().string();
            if (!fs::is_regular_file(binPath)) return fail(lang::t("binary_not_found") + binPath);
            std::cout << lang::t("using_bin") << binPath << std::endl;
        } else {
            std::cout << lang::t("detect") << projName(proj) << std::endl;
            std::cout << lang::t("building") << std::endl;
            binPath = buildProj(proj, c.dir, c.binary);
            std::cout << lang::t("built") << binPath << std::endl;
        }

        std::string binRel;
        if (binPath.rfind(c.dir + "/", 0) == 0) binRel = binPath.substr(c.dir.size() + 1);

        auto info = inspectBinary(binPath);
        if (info.elf && info.arch.empty() && c.arch.empty())
            return fail(lang::t("not_elf") + binPath);
        if (!info.elf && !info.script && c.arch.empty())
            return fail(lang::t("not_elf") + binPath);
        if (c.arch.empty()) c.arch = info.arch;
        else if (!info.arch.empty() && info.arch != c.arch && !(info.script && c.arch != "all"))
            return fail(lang::t("arch_mismatch") + c.arch + " / " + info.arch);
        std::cout << lang::t("arch") << c.arch << std::endl;

        if (auto p = portabilityProblem(info); !p.empty())
            std::cerr << lang::t("warning") << lang::t("not_portable") << p << lang::t("not_portable_tail") << "\n";

        TempDir work(cacheDir() + "/work", c.name);
        auto stage = work.path() + "/stage";
        makeStage(c, binPath, stage);

        bool split = outDir.empty();
        std::string base = split ? homeDir() + "/pb-builds" : fs::absolute(outDir).string();
        auto outFor = [&](const std::string& fmt) {
            auto d = split ? base + "/" + fmt : base;
            fs::create_directories(d);
            return d;
        };

        struct Job {
            std::string fmt;
            std::future<std::string> result;
        };
        std::vector<Job> jobs;
        const Config& cfg = c;
        auto launch = [&](const std::string& fmt, std::function<std::string()> fn) {
            jobs.push_back({fmt, std::async(std::launch::async, fn)});
        };

        std::cout << lang::t("packing") << std::endl;
        if (formats.count("deb")) {
            auto out = outFor("deb");
            fs::create_directories(work.path() + "/deb-work");
            launch("deb", [&, out] { return packDeb(cfg, stage, work.path() + "/deb-work", out); });
        }
        if (formats.count("rpm")) {
            auto out = outFor("rpm");
            fs::create_directories(work.path() + "/rpm-work");
            launch("rpm", [&, out] { return packRpm(cfg, stage, work.path() + "/rpm-work", out); });
        }
        if (formats.count("appimage")) {
            auto out = outFor("appimage");
            fs::create_directories(work.path() + "/appimage-work");
            launch("appimage", [&, out] { return packAppImage(cfg, stage, work.path() + "/appimage-work", out); });
        }
        if (formats.count("aur")) {
            auto out = outFor("aur");
            launch("aur", [&, out] { return packAur(cfg, proj, binRel, stage, out); });
        }

        std::vector<std::string> failed;
        for (auto& j : jobs) {
            try {
                auto path = j.result.get();
                std::cout << "  [" << j.fmt << "] -> " << path << std::endl;
            } catch (const fs::filesystem_error& e) {
                std::cerr << "  [" << j.fmt << "] " << lang::t("cant_write") << e.path1().string() << std::endl;
                failed.push_back(j.fmt);
            } catch (const std::exception& e) {
                std::string msg = e.what();
                if (msg == "unknown_lang") msg = lang::t("unknown_lang");
                std::cerr << "  [" << j.fmt << "] " << msg << std::endl;
                failed.push_back(j.fmt);
            }
        }

        if (!failed.empty()) {
            std::string list;
            for (auto& f : failed) list += (list.empty() ? "" : ", ") + f;
            std::cerr << lang::t("failed_formats") << list << "\n";
            return 1;
        }
        std::cout << lang::t("done") << base << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.rfind("no_config:", 0) == 0) return fail(lang::t("no_config") + msg.substr(10));
        if (msg == "unknown_lang") return fail(lang::t("unknown_lang"));
        return fail(msg);
    }
}

int setLang(int argc, char** argv) {
    std::string code = argc >= 3 ? argv[2] : "";
    if (argc != 3 || (code != "ru" && code != "en")) {
        std::cerr << lang::t("lang_usage") << "\n";
        return 1;
    }
    if (!lang::set(code)) return fail(lang::t("lang_write") + lang::file());
    return 0;
}

}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd.empty() || cmd == "-h" || cmd == "--help" || cmd == "help") {
        help();
        return 0;
    }
    if (cmd == "--version" || cmd == "version") {
        std::cout << "pb " << PB_VERSION << "\n";
        return 0;
    }
    if (cmd == "build") return build(argc, argv);
    if (cmd == "lang") return setLang(argc, argv);
    if (cmd == "install" || cmd == "repair") {
        if (argc > 2) return fail(lang::t("unknown_arg") + argv[2]);
        return cmd == "install" ? cmdInstall() : cmdRepair();
    }
    if (cmd == "uninstall" || cmd == "update") {
        bool flag = false;
        std::string want = cmd == "uninstall" ? "--purge" : "--yes";
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == want || (cmd == "update" && a == "-y")) flag = true;
            else return fail(lang::t("unknown_arg") + a);
        }
        return cmd == "uninstall" ? cmdUninstall(flag) : cmdUpdate(flag);
    }
    std::cerr << lang::t("error") << lang::t("unknown_cmd") << cmd << "\n";
    help();
    return 1;
}
