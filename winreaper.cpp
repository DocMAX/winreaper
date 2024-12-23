#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <string>

struct ProcessInfo {
    DWORD pid;
    std::wstring name;
    DWORD parentPid;
    std::wstring commandLine;
    bool isValid;
};

bool isProcessRunning(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess == NULL) return false;
    
    DWORD exitCode;
    bool isRunning = GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(hProcess);
    return isRunning;
}

// Move getProcessInfo before ProcessTree
ProcessInfo getProcessInfo(DWORD pid) {
    ProcessInfo info = { pid, L"", 0, L"", false };
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &entry)) {
            do {
                if (entry.th32ProcessID == pid) {
                    info.name = entry.szExeFile;
                    info.parentPid = entry.th32ParentProcessID;
                    info.isValid = true;
                    break;
                }
            } while (Process32NextW(hSnapshot, &entry));
        }
        CloseHandle(hSnapshot);
    }
    
    if (info.name.empty() && info.isValid) {
        info.name = L"<unknown>";
    }

    return info;
}

class ProcessTree {
private:
    std::unordered_map<DWORD, ProcessInfo> processes;
    DWORD rootPid;
    DWORD gameProcessPid;
    std::wstring initialCommand;

public:
    ProcessTree(DWORD initialPid, const std::wstring& cmd) 
        : rootPid(initialPid), gameProcessPid(0), initialCommand(cmd) {
        // Add the current process (winreaper) to the tracked processes
        ProcessInfo self = getProcessInfo(GetCurrentProcessId());
        self.commandLine = initialCommand;
        processes[GetCurrentProcessId()] = self;
    }

    bool shouldTrack(DWORD pid, DWORD parentPid) const {
        // Track if it's the root process
        if (pid == rootPid) return true;
        
        // Track if it's a child of any process we're already tracking
        if (processes.find(parentPid) != processes.end()) return true;
        
        // Track if it's the game process or its children
        if (gameProcessPid != 0 && (pid == gameProcessPid || isDescendant(parentPid))) return true;

        return false;
    }

    bool isDescendant(DWORD pid) const {
        if (pid == 0) return false;
        
        DWORD currentPid = pid;
        int depth = 0;  // Prevent infinite loops
        while (currentPid != 0 && depth < 10) {
            if (currentPid == rootPid || currentPid == gameProcessPid) return true;
            auto it = processes.find(currentPid);
            if (it == processes.end()) break;
            currentPid = it->second.parentPid;
            depth++;
        }
        return false;
    }

    void addProcess(const ProcessInfo& info) {
        processes[info.pid] = info;
        
        // If this is the game process, track it
        if (info.name.find(L"APlagueTaleInnocence_x64.exe") != std::wstring::npos) {
            gameProcessPid = info.pid;
        }
    }

    bool isTracked(DWORD pid) const {
        return processes.find(pid) != processes.end();
    }

    bool isWatchedForExit(DWORD pid) const {
        return pid == gameProcessPid || isDescendant(pid);
    }

    std::wstring getProcessName(DWORD pid) const {
        if (pid == GetCurrentProcessId()) {
            return L"winreaper.exe";  // Always return our own name
        }
        auto it = processes.find(pid);
        return it != processes.end() ? it->second.name : L"<unknown>";
    }

    bool isRunning() const {
        for (const auto& pair : processes) {
            if (isProcessRunning(pair.first)) {
                if (pair.first == gameProcessPid || isDescendant(pair.first)) {
                    return true;
                }
            }
        }
        return false;
    }
};

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcerr << L"Usage: " << argv[0] << L" <program> [args...]\n";
        return 1;
    }

    std::wstring commandLine = argv[1];
    for (int i = 2; i < argc; ++i) {
        commandLine += L" ";
        commandLine += argv[i];
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (!CreateProcessW(NULL, commandLine.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::wcerr << L"Failed to create process: " << GetLastError() << L"\n";
        return 1;
    }

    ProcessTree processTree(pi.dwProcessId, commandLine);
    std::unordered_map<DWORD, ProcessInfo> currentProcesses;
    std::unordered_map<DWORD, ProcessInfo> previousProcesses;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (true) {
        currentProcesses.clear();
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry;
            entry.dwSize = sizeof(PROCESSENTRY32W);

            if (Process32FirstW(hSnapshot, &entry)) {
                do {
                    ProcessInfo info = { 
                        entry.th32ProcessID,
                        entry.szExeFile,
                        entry.th32ParentProcessID,
                        L"",
                        true
                    };
                    currentProcesses[entry.th32ProcessID] = info;

                    if (previousProcesses.find(entry.th32ProcessID) == previousProcesses.end() && 
                        processTree.shouldTrack(entry.th32ProcessID, entry.th32ParentProcessID)) {
                        
                        processTree.addProcess(info);
                        
                        std::wcout << L"[+] New process: " << info.name
                                  << L" (PID: " << info.pid
                                  << L", Parent: " << processTree.getProcessName(info.parentPid)
                                  << L" [" << info.parentPid << L"])";
                        
                        // Add indicator if this process is being watched for program exit
                        if (processTree.isWatchedForExit(info.pid)) {
                            std::wcout << L" [TRACKED]";
                        }
                        std::wcout << L"\n";
                    }
                } while (Process32NextW(hSnapshot, &entry));
            }
            CloseHandle(hSnapshot);
        }

        // Check for ended processes
        for (const auto& prev : previousProcesses) {
            if ((currentProcesses.find(prev.first) == currentProcesses.end() || !isProcessRunning(prev.first)) 
                && processTree.isTracked(prev.first)) {
                
                const ProcessInfo& info = prev.second;
                std::wcout << L"[-] Process ended: " << info.name 
                          << L" (PID: " << info.pid << L")";
                
                if (processTree.isWatchedForExit(info.pid)) {
                    std::wcout << L" [TRACKED]";
                }
                std::wcout << L"\n";
            }
        }

        previousProcesses = currentProcesses;

        if (!processTree.isRunning()) {
            std::wcout << L"All tracked processes have ended. Exiting...\n";
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
