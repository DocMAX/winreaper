#include <windows.h>
#include <iostream>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <chrono>
#include <thread>

std::wstring getProcessName(DWORD pid) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to take process snapshot: " << GetLastError() << L"\n";
        return L"Unknown";
    }

    if (Process32First(hSnapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                CloseHandle(hSnapshot);
                return entry.szExeFile;
            }
        } while (Process32Next(hSnapshot, &entry));
    }

    CloseHandle(hSnapshot);
    return L"Unknown";
}

void waitForProcess(DWORD pid) {
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (hProcess == NULL) {
        std::wcerr << L"Failed to open process with PID " << pid << L": " << GetLastError() << L"\n";
        return;
    }

    // Wait for the process to finish
    WaitForSingleObject(hProcess, INFINITE);
    CloseHandle(hProcess);
}

std::vector<DWORD> getChildProcesses(DWORD parentPid) {
    std::vector<DWORD> childPids;
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to take process snapshot: " << GetLastError() << L"\n";
        return childPids;
    }

    if (Process32First(hSnapshot, &entry)) {
        do {
            if (entry.th32ParentProcessID == parentPid) {
                childPids.push_back(entry.th32ProcessID);
            }
        } while (Process32Next(hSnapshot, &entry));
    }
    CloseHandle(hSnapshot);

    return childPids;
}

void logProcessStatus(const std::set<DWORD>& previousPids, const std::set<DWORD>& currentPids) {
    for (DWORD pid : previousPids) {
        if (currentPids.find(pid) == currentPids.end()) {
            std::wcout << L"Process with PID " << pid << L" (" << getProcessName(pid) << L") has exited.\n";
        }
    }

    for (DWORD pid : currentPids) {
        if (previousPids.find(pid) == previousPids.end()) {
            std::wcout << L"New process with PID " << pid << L" (" << getProcessName(pid) << L") has started.\n";
        }
    }
}

void monitorSubProcesses(DWORD parentPid, bool& exitFlag) {
    std::set<DWORD> currentChildPids = { parentPid };
    while (!exitFlag) {
        std::vector<DWORD> childPids = getChildProcesses(parentPid);

        std::set<DWORD> newChildPids(childPids.begin(), childPids.end());

        logProcessStatus(currentChildPids, newChildPids);

        currentChildPids = newChildPids;

        // Wait for 1 second to update the list of subprocesses regularly
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void waitForAllProcesses(DWORD parentPid) {
    // Wait for the main process to finish
    waitForProcess(parentPid);

    // Then wait for all child processes
    std::vector<DWORD> childPids = getChildProcesses(parentPid);
    for (DWORD childPid : childPids) {
        waitForProcess(childPid);
    }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcerr << L"Usage: " << argv[0] << L" <program>\n";
        return 1;
    }

    wchar_t* program = argv[1];
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;

    // Start the process
    if (!CreateProcess(
        NULL,       // Application path
        program,    // Command to run the program
        NULL,       // Process security attributes
        NULL,       // Thread security attributes
        FALSE,      // No inheritance of handles
        CREATE_NEW_CONSOLE, // New console window
        NULL,       // Environment (unchanged)
        NULL,       // Current directory (unchanged)
        &si,        // STARTUPINFO structure
        &pi         // PROCESS_INFORMATION structure
    )) {
        std::wcerr << L"Failed to start process: " << GetLastError() << L"\n";
        return 1;
    }

    std::wcout << L"Main process started: " << program << L"\n";

    // Flag to exit the monitoring thread
    bool exitFlag = false;

    // Start the process monitoring thread
    std::thread monitorThread(monitorSubProcesses, pi.dwProcessId, std::ref(exitFlag));

    // Wait for the main process and all its child processes
    waitForAllProcesses(pi.dwProcessId);

    std::wcout << L"Process and its children finished successfully.\n";

    // Set the exit flag to stop the monitoring thread
    exitFlag = true;

    // Wait for the monitoring thread to finish
    monitorThread.join();

    // Close handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}
