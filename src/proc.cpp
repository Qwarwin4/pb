#include "proc.hpp"
#include "lang.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

std::string findInPath(const std::string& tool) {
    auto runnable = [](const std::string& p) {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(p.c_str(), X_OK) == 0;
    };
    if (tool.find('/') != std::string::npos) return runnable(tool) ? tool : "";

    const char* env = std::getenv("PATH");
    std::string path = env && *env ? env : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(start, end - start);
        std::string full = (dir.empty() ? "." : dir) + "/" + tool;
        if (runnable(full)) return full;
        start = end + 1;
    }
    return "";
}

bool exists(const std::string& tool) { return !findInPath(tool).empty(); }

namespace {

std::string joined(const std::vector<std::string>& args) {
    std::string s;
    for (auto& a : args) s += (s.empty() ? "" : " ") + a;
    return s;
}

pid_t spawn(const std::string& exe, const std::vector<std::string>& args,
            const std::string& cwd, int outFd, int errFd) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);

    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork: " + std::string(std::strerror(errno)));
    if (pid == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(126);
        if (outFd >= 0) dup2(outFd, STDOUT_FILENO);
        if (errFd >= 0) dup2(errFd, STDERR_FILENO);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    return pid;
}

int waitFor(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + WTERMSIG(status);
}

int devNull() { return open("/dev/null", O_WRONLY | O_CLOEXEC); }

}

int run(const std::vector<std::string>& args, const std::string& cwd, bool quiet) {
    std::string exe = findInPath(args.at(0));
    if (exe.empty()) return 127;
    int null = quiet ? devNull() : -1;
    pid_t pid = spawn(exe, args, cwd, null, null);
    if (null >= 0) close(null);
    return waitFor(pid);
}

void must(const std::vector<std::string>& args, const std::string& cwd) {
    if (!exists(args.at(0))) throw std::runtime_error(lang::t("not_found") + args[0]);
    int rc = run(args, cwd);
    if (rc != 0) throw std::runtime_error(lang::t("cmd_failed") + std::to_string(rc) + "): " + joined(args));
}

std::string capture(const std::vector<std::string>& args, bool quietErr) {
    std::string exe = findInPath(args.at(0));
    if (exe.empty()) throw std::runtime_error(lang::t("not_found") + args[0]);

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) throw std::runtime_error("pipe: " + std::string(std::strerror(errno)));
    int null = quietErr ? devNull() : -1;
    pid_t pid;
    try {
        pid = spawn(exe, args, "", fds[1], null);
    } catch (...) {
        close(fds[0]);
        close(fds[1]);
        if (null >= 0) close(null);
        throw;
    }
    close(fds[1]);
    if (null >= 0) close(null);

    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n > 0) out.append(buf, (size_t)n);
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    close(fds[0]);

    int rc = waitFor(pid);
    if (rc != 0) throw std::runtime_error(lang::t("cmd_failed") + std::to_string(rc) + "): " + joined(args));
    return out;
}
