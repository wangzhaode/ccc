#include "bash_tool.hpp"
#include <cstdio>
#include <array>
#include <memory>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <chrono>
#include <thread>

ToolResult BashTool::execute(const json& params) {
  std::string command = params.at("command").get<std::string>();
  int timeout_ms = params.value("timeout", 120000);

  // Create pipes for capturing output
  HANDLE hReadPipe, hWritePipe;
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
    return {"Error: Failed to create pipe", true};
  }
  SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  // Set up process info
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = hWritePipe;
  si.hStdOutput = hWritePipe;
  si.dwFlags |= STARTF_USESTDHANDLES;

  ZeroMemory(&pi, sizeof(pi));

  // Use cmd.exe to run the command
  std::string cmd = "cmd /c " + command;

  // CreateProcess requires a mutable string
  char* cmdLine = new char[cmd.size() + 1];
  strcpy(cmdLine, cmd.c_str());

  BOOL success = CreateProcessA(
      nullptr,
      cmdLine,
      nullptr,
      nullptr,
      TRUE,
      0,
      nullptr,
      nullptr,
      &si,
      &pi
  );

  delete[] cmdLine;
  CloseHandle(hWritePipe);

  if (!success) {
    CloseHandle(hReadPipe);
    return {"Error: Failed to create process", true};
  }

  // Read output with timeout
  std::string output;
  std::array<char, 4096> buffer;
  DWORD bytesRead;
  bool timed_out = false;

  auto start = std::chrono::steady_clock::now();

  while (true) {
    DWORD available = 0;
    if (!PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &available, nullptr)) {
      break;
    }

    if (available == 0) {
      // Check if process has terminated
      DWORD exitCode;
      if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
        // Read any remaining data
        while (ReadFile(hReadPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
          output.append(buffer.data(), bytesRead);
        }
        break;
      }

      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      if (elapsed >= timeout_ms) {
        timed_out = true;
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (!ReadFile(hReadPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0) {
      break;
    }
    output.append(buffer.data(), bytesRead);

    if (output.size() > 1024 * 1024) {
      output += "\n... (output truncated at 1MB)";
      break;
    }
  }

  CloseHandle(hReadPipe);

  if (timed_out) {
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return {"Error: Command timed out after " + std::to_string(timeout_ms) + "ms\n" + output, true};
  }

  DWORD exitCode;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (exitCode != 0) {
    return {"Exit code: " + std::to_string(exitCode) + "\n" + output, true};
  }

  return {output, false};
}

#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/select.h>

ToolResult BashTool::execute(const json& params) {
  std::string command = params.at("command").get<std::string>();
  int timeout_ms = params.value("timeout", 120000);

  // Use pipe to capture output
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    return {"Error: Failed to create pipe", true};
  }

  pid_t pid = fork();
  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    return {"Error: Failed to fork process", true};
  }

  if (pid == 0) {
    // Child process
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    execl("/bin/bash", "bash", "-c", command.c_str(), nullptr);
    _exit(127);
  }

  // Parent process
  close(pipefd[1]);

  // Read output with timeout
  std::string output;
  std::array<char, 4096> buffer;
  bool timed_out = false;

  auto start = std::chrono::steady_clock::now();

  fd_set fds;
  struct timeval tv;

  while (true) {
    FD_ZERO(&fds);
    FD_SET(pipefd[0], &fds);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    int remaining_ms = timeout_ms - (int)elapsed;

    if (remaining_ms <= 0) {
      timed_out = true;
      break;
    }

    tv.tv_sec = remaining_ms / 1000;
    tv.tv_usec = (remaining_ms % 1000) * 1000;

    int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
    if (ret <= 0) {
      if (ret == 0)
        timed_out = true;
      break;
    }

    ssize_t n = read(pipefd[0], buffer.data(), buffer.size());
    if (n <= 0)
      break;
    output.append(buffer.data(), n);

    if (output.size() > 1024 * 1024) {
      output += "\n... (output truncated at 1MB)";
      break;
    }
  }

  close(pipefd[0]);

  if (timed_out) {
    kill(pid, SIGTERM);
    usleep(100000);  // 100ms grace period
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    return {"Error: Command timed out after " + std::to_string(timeout_ms) + "ms\n" + output, true};
  }

  int status;
  waitpid(pid, &status, 0);
  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  if (exit_code != 0) {
    return {"Exit code: " + std::to_string(exit_code) + "\n" + output, true};
  }

  return {output, false};
}
#endif
