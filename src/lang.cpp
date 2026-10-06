#include "lang.hpp"
#include "fsutil.hpp"
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace {

using Dict = std::unordered_map<std::string, std::pair<std::string, std::string>>;

const Dict& dict() {
    static const Dict d = {
        {"pkg_info", {"пакет: ", "package: "}},
        {"detect", {"тип проекта: ", "project type: "}},
        {"building", {"собираю проект...", "building project..."}},
        {"built", {"собрано: ", "built: "}},
        {"using_bin", {"использую готовый бинарник: ", "using prebuilt binary: "}},
        {"arch", {"архитектура: ", "architecture: "}},
        {"packing", {"собираю пакеты...", "packing..."}},
        {"done", {"готово, результаты здесь: ", "done, results here: "}},
        {"failed_formats", {"не собрались: ", "failed: "}},
        {"error", {"ошибка: ", "error: "}},
        {"warning", {"предупреждение: ", "warning: "}},

        {"no_config", {"не найден файл build.toml: ", "build.toml not found: "}},
        {"cfg_parse", {"не могу разобрать строку ", "cannot parse line "}},
        {"cfg_unknown", {"неизвестный ключ, пропускаю: ", "unknown key, ignored: "}},
        {"cfg_no_name", {"не указано поле name", "the name field is missing"}},
        {"cfg_name", {"name: только строчные латинские буквы, цифры и + . -, от 2 символов: ",
                      "name: lowercase latin letters, digits and + . - only, at least 2 chars: "}},
        {"cfg_version", {"version должна начинаться с цифры и содержать только буквы, цифры и . + ~: ",
                         "version must start with a digit and contain only letters, digits and . + ~: "}},
        {"cfg_binary", {"binary: только имя файла, без / и пробелов: ",
                        "binary: a plain file name, no / or spaces: "}},
        {"cfg_arch", {"arch: допустимо amd64, arm64, i386, armhf, riscv64 или all: ",
                      "arch: allowed values are amd64, arm64, i386, armhf, riscv64, all: "}},
        {"cfg_ctrl", {"в значении есть перевод строки или управляющий символ: ",
                      "value contains a newline or control character: "}},
        {"cfg_dep", {"некорректная зависимость: ", "invalid dependency: "}},
        {"cfg_main", {"main: каталог не найден или путь выходит за пределы проекта: ",
                      "main: directory not found or outside the project: "}},
        {"go_no_main", {"не найден package main ни в одной папке проекта: ",
                        "no package main found anywhere in the project: "}},
        {"go_many_main", {"в проекте несколько package main, укажи нужный в build.toml:",
                          "several main packages found, pick one in build.toml:"}},
        {"proj_above", {"build.toml должен лежать в корне проекта, рядом с go.mod / Cargo.toml / CMakeLists.txt / Makefile. "
                        "Перенеси его сюда: ",
                        "build.toml must sit in the project root next to go.mod / Cargo.toml / CMakeLists.txt / Makefile. "
                        "Move it here: "}},
        {"cfg_icon_missing", {"иконка не найдена: ", "icon not found: "}},
        {"cfg_icon_ext", {"иконка должна быть .png или .svg: ", "icon must be .png or .svg: "}},
        {"cfg_icon_cli", {"icon задан, но gui = false — консольной программе иконка не нужна, пропускаю",
                          "icon is set but gui = false — a CLI program needs no icon, skipping"}},

        {"unknown_lang", {"неизвестный тип проекта. Нужен CMakeLists.txt, Cargo.toml, go.mod или Makefile, "
                          "либо укажи --binary путь к готовому бинарнику",
                          "unknown project type. Need CMakeLists.txt, Cargo.toml, go.mod or Makefile, "
                          "or pass --binary with a prebuilt binary"}},
        {"bin_missing", {"бинарник не найден после сборки (проверь поле binary в build.toml): ",
                         "binary not found after build (check the binary field in build.toml): "}},
        {"binary_not_found", {"бинарник не найден: ", "binary not found: "}},
        {"not_elf", {"это не ELF и не скрипт, архитектуру не определить — укажи arch в build.toml: ",
                     "neither ELF nor script, cannot detect architecture — set arch in build.toml: "}},
        {"arch_mismatch", {"arch в build.toml не совпадает с бинарником: ",
                           "arch in build.toml does not match the binary: "}},
        {"not_portable", {"бинарник собран динамически и требует ",
                          "the binary is dynamically linked and requires "}},
        {"not_portable_tail", {" — на старых дистрибутивах (Debian 11, Ubuntu 20.04, RHEL 8) он не запустится. "
                               "Для переносимых пакетов собирай статически или на старом дистрибутиве.",
                               " — it will not run on older distros (Debian 11, Ubuntu 20.04, RHEL 8). "
                               "For portable packages build statically or on an older distro."}},
        {"runtime_arch", {"нет runtime AppImage для архитектуры ", "no AppImage runtime for architecture "}},
        {"runtime_bad", {"runtime AppImage повреждён или не той архитектуры: ",
                         "AppImage runtime is corrupt or of a different architecture: "}},
        {"need_curl_wget", {"нужен curl или wget", "curl or wget is required"}},
        {"need_curl_wget_runtime", {"нужен curl или wget, чтобы один раз скачать runtime AppImage "
                                    "(либо укажи готовый файл в PB_APPIMAGE_RUNTIME)",
                                    "curl or wget is needed to fetch the AppImage runtime once "
                                    "(or point PB_APPIMAGE_RUNTIME at a local copy)"}},

        {"cmd_failed", {"команда упала (код ", "command failed (code "}},
        {"not_found", {"команда не найдена: ", "command not found: "}},
        {"cant_write", {"не смог записать: ", "cannot write: "}},
        {"needs_value", {" требует значение", " needs a value"}},
        {"unknown_arg", {"неизвестный аргумент: ", "unknown argument: "}},
        {"unknown_cmd", {"неизвестная команда: ", "unknown command: "}},

        {"installed", {"pb установлен: ", "pb installed: "}},
        {"already", {"pb уже установлен, это тот же файл", "pb is already installed, same file"}},
        {"not_installed", {"pb не установлен в ", "pb is not installed in "}},
        {"removed", {"pb удалён", "pb removed"}},
        {"purged", {"настройки и кэш удалены", "config and cache removed"}},
        {"managed", {"этот pb поставлен пакетным менеджером — обновляй и удаляй его через него: ",
                     "this pb was installed by a package manager — update and remove it there: "}},
        {"need_root", {"не хватает прав на ", "not enough permissions for "}},
        {"need_root_hint", {"запусти через sudo или doas", "run it with sudo or doas"}},

        {"repair_perm", {"исправил права: ", "fixed permissions: "}},
        {"repair_lang", {"файл языка был повреждён, сбросил на ru", "language file was corrupt, reset to ru"}},
        {"repair_lang_ro", {"файл языка недоступен на запись (вероятно, создан от root): ",
                            "language file is not writable (probably created by root): "}},
        {"repair_cache", {"удалил повреждённый runtime: ", "removed corrupt runtime: "}},
        {"repair_ok", {"проблем не найдено", "no problems found"}},

        {"lang_usage", {"использование: pb lang ru|en", "usage: pb lang ru|en"}},
        {"lang_write", {"не удалось сохранить язык в ", "cannot save the language to "}},

        {"checking", {"проверяю релизы: ", "checking releases: "}},
        {"fetch_failed", {"не удалось получить данные о релизах: нет сети, лимит запросов GitHub "
                          "или у репозитория ещё нет релизов",
                          "could not fetch release data: no network, GitHub rate limit, "
                          "or the repository has no releases yet"}},
        {"no_tag", {"в ответе GitHub нет тега релиза", "no release tag in the GitHub response"}},
        {"up_to_date", {"уже последняя версия: ", "already up to date: "}},
        {"local_newer", {"установленная версия новее последнего релиза: ",
                         "installed version is newer than the latest release: "}},
        {"new_version", {"новая версия: ", "new version: "}},
        {"current", {", сейчас ", ", current "}},
        {"confirm", {"обновить? [y/N] ", "update? [y/N] "}},
        {"need_yes", {"нет терминала для подтверждения — запусти pb update --yes",
                      "no terminal to confirm — run pb update --yes"}},
        {"downloading", {"скачиваю ", "downloading "}},
        {"no_asset", {"в релизе нет бинарника для этой архитектуры: ",
                      "the release has no binary for this architecture: "}},
        {"bad_checksum", {"контрольная сумма не совпала, обновление отменено",
                          "checksum mismatch, update aborted"}},
        {"bad_binary", {"скачанный бинарник не запускается, обновление отменено",
                        "the downloaded binary does not run, update aborted"}},
        {"updated", {"обновлено до ", "updated to "}},
        {"appimage_update", {"pb запущен из AppImage — скачай новый AppImage: ",
                             "pb is running from an AppImage — download a new one: "}},
    };
    return d;
}

std::string& current() {
    static std::string code = [] {
        std::ifstream f(lang::file());
        std::string s;
        if (f) std::getline(f, s);
        return s == "en" ? std::string("en") : std::string("ru");
    }();
    return code;
}

}

std::string lang::file() { return configDir() + "/lang"; }

std::string lang::get() { return current(); }

bool lang::set(const std::string& code) {
    std::error_code ec;
    std::filesystem::create_directories(configDir(), ec);
    std::ofstream f(file(), std::ios::trunc);
    f << code << "\n";
    f.close();
    if (!f) return false;
    current() = code;
    return true;
}

std::string lang::t(const std::string& key) {
    auto it = dict().find(key);
    if (it == dict().end()) return key;
    return current() == "en" ? it->second.second : it->second.first;
}
