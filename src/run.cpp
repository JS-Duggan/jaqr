#include "run.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <random>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace {
namespace fs = std::filesystem;

bool executable(const fs::path &path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        return false;
#ifdef _WIN32
    return true;
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}

fs::path find_on_path(const std::string &name) {
    if (const char *path = std::getenv("PATH")) {
        const std::string directories(path);
#ifdef _WIN32
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        for (std::size_t start = 0; start < directories.size();) {
            const auto end = directories.find(separator, start);
            const auto directory = directories.substr(start, end - start);
            if (!directory.empty()) {
                const auto candidate = fs::absolute(fs::path(directory) / name);
                if (executable(candidate))
                    return candidate;
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return {};
}

fs::path find_compiler() {
    if (const char *configured = std::getenv("JAQR_CXX"); configured && *configured) {
        const fs::path compiler(configured);
        const auto found =
            compiler.has_parent_path() ? fs::absolute(compiler) : find_on_path(compiler.string());
        if (!executable(found))
            throw std::runtime_error("JAQR_CXX does not name an executable C++ compiler");
        return found;
    }
#ifdef _WIN32
    const std::vector<std::string> candidates = {"g++.exe", "clang++.exe"};
#elif defined(__APPLE__)
    const std::vector<std::string> candidates = {"clang++", "g++"};
#else
    const std::vector<std::string> candidates = {"g++", "clang++"};
#endif
    for (const auto &name : candidates)
        if (auto compiler = find_on_path(name); !compiler.empty())
            return compiler;
    throw std::runtime_error("No C++ compiler found on PATH; set JAQR_CXX to a C++20 compiler");
}

fs::path make_temp_dir() {
    const auto base = fs::temp_directory_path();
#ifdef _WIN32
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto path =
            base / ("jaqr-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;
        if (fs::create_directory(path, error))
            return path;
        if (error && error != std::errc::file_exists)
            throw fs::filesystem_error("Could not create temporary directory", path, error);
    }
    throw std::runtime_error("Could not create a unique temporary directory");
#else
    std::string pattern = (base / "jaqr-XXXXXX").string();
    if (!mkdtemp(pattern.data()))
        throw std::system_error(errno, std::generic_category(),
                                "Could not create temporary directory");
    return pattern;
#endif
}

struct temporary_directory {
    fs::path path = make_temp_dir();

    ~temporary_directory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

// Pass arguments directly to the process API instead of constructing a shell command.
// Paths containing spaces stay intact, and the child inherits terminal input/output.
int execute(const std::vector<fs::path> &arguments) {
#ifdef _WIN32
    std::vector<const wchar_t *> argv;
    for (const auto &argument : arguments)
        argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const intptr_t status = _wspawnv(_P_WAIT, arguments.front().c_str(), argv.data());
    if (status == -1)
        throw std::system_error(errno, std::generic_category(), "Could not start process");
    return static_cast<int>(status);
#else
    std::vector<char *> argv;
    for (const auto &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child;
    const int error =
        posix_spawn(&child, arguments.front().c_str(), nullptr, nullptr, argv.data(), environ);
    if (error)
        throw std::system_error(error, std::generic_category(), "Could not start process");
    int status;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "Could not wait for process");
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
#endif
}
} // namespace

int run_generated(const std::vector<std::string> &code, const std::string &output_name, bool run) {
    // Generation always leaves inspectable C++ on disk. Compiling and executing it is
    // opt-in; only the executable is temporary, so diagnostics can refer to this source.
    const auto source = fs::absolute(output_name);
    {
        std::ofstream output(source);
        if (!output)
            throw std::runtime_error("Could not write generated C++");
        for (const auto &part : code)
            if (!part.empty())
                output << part << '\n';
        output.close();
        if (!output)
            throw std::runtime_error("Could not finish generated C++");
    }
    if (!run)
        return 0;

    const auto compiler = find_compiler();
    temporary_directory temp;
#ifdef _WIN32
    const auto program = temp.path / "program.exe";
#else
    const auto program = temp.path / "program";
#endif
    const int compiled =
        execute({compiler, "-std=c++20", "-I", JAQR_RUNTIME_DIR, source, "-o", program});
    return compiled ? compiled : execute({program});
}
