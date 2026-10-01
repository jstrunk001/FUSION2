#include "fusion/batch/ProcessRunner.h"

#include <windows.h>
#include <sstream>

namespace fusion::batch {

// Quotes a single command-line argument using the same rule CommandLineToArgvW
// (and therefore every CRT's argv parser) expects, so arguments containing
// spaces or quotes survive the round trip through CreateProcess intact.
static std::string QuoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }

    std::string result = "\"";
    for (auto it = arg.begin(); ; ++it) {
        size_t numBackslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++it;
            ++numBackslashes;
        }

        if (it == arg.end()) {
            result.append(numBackslashes * 2, '\\');
            break;
        } else if (*it == '"') {
            result.append(numBackslashes * 2 + 1, '\\');
            result.push_back(*it);
        } else {
            result.append(numBackslashes, '\\');
            result.push_back(*it);
        }
    }
    result.push_back('"');
    return result;
}

static std::string BuildCommandLine(const std::filesystem::path& exePath, const std::vector<std::string>& args) {
    std::ostringstream cmd;
    cmd << QuoteArg(exePath.string());
    for (const auto& arg : args) {
        cmd << " " << QuoteArg(arg);
    }
    return cmd.str();
}

ProcessResult RunProcess(
    const std::filesystem::path& exePath,
    const std::vector<std::string>& args,
    const std::filesystem::path& logPath) {

    ProcessResult result;

    if (!logPath.empty()) {
        std::filesystem::create_directories(logPath.parent_path());
    }

    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    HANDLE logHandle = logPath.empty()
        ? nullptr
        : CreateFileA(
            logPath.string().c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &saAttr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    // When the child's output goes to a log file it needs no console of its
    // own. Without CREATE_NO_WINDOW, a parent that has no console (started
    // hidden, from a scheduled task, or from a service) makes Windows open a
    // new console window for every child -- one per tile and stage, each
    // taking keyboard and mouse focus. Without a log file the child keeps
    // sharing the parent's console, so its output still appears there.
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOA);
    DWORD creationFlags = 0;
    if (logHandle && logHandle != INVALID_HANDLE_VALUE) {
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = logHandle;
        startupInfo.hStdError = logHandle;
        creationFlags |= CREATE_NO_WINDOW;
    }

    PROCESS_INFORMATION processInfo{};
    std::string commandLine = BuildCommandLine(exePath, args);

    // CreateProcess requires a mutable command-line buffer.
    std::vector<char> cmdBuffer(commandLine.begin(), commandLine.end());
    cmdBuffer.push_back('\0');

    BOOL ok = CreateProcessA(
        exePath.string().c_str(),
        cmdBuffer.data(),
        nullptr,
        nullptr,
        /*bInheritHandles=*/TRUE,
        creationFlags,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (logHandle && logHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(logHandle);
    }

    if (!ok) {
        result.launched = false;
        result.exitCode = -1;
        return result;
    }

    result.launched = true;
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return result;
}

std::filesystem::path GetExecutableDir() {
    char buffer[MAX_PATH]{};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
}

} // namespace fusion::batch
