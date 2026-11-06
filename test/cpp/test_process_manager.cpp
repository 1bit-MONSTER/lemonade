#include <lemon/utils/process_manager.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

using lemon::utils::ProcessHandle;
using lemon::utils::ProcessManager;

namespace {

int failures = 0;

void check(const char* name, bool condition) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) {
        ++failures;
    }
}

ProcessHandle make_handle(pid_t pid) {
    return {nullptr, static_cast<int>(pid)};
}

pid_t spawn_exiting_child(int exit_code) {
    const pid_t pid = fork();
    if (pid == 0) {
        _exit(exit_code);
    }
    return pid;
}

pid_t spawn_running_child() {
    const pid_t pid = fork();
    if (pid == 0) {
        for (;;) {
            pause();
        }
    }
    return pid;
}

bool is_zombie(pid_t pid) {
    std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
    std::string stat_line;
    if (!std::getline(stat_file, stat_line)) {
        return false;
    }

    const auto close_paren = stat_line.rfind(')');
    return close_paren != std::string::npos &&
           close_paren + 2 < stat_line.size() &&
           stat_line[close_paren + 2] == 'Z';
}

bool wait_for_zombie(pid_t pid, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (is_zombie(pid)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);

    return is_zombie(pid);
}

void kill_and_reap(pid_t pid) {
    if (pid <= 0) {
        return;
    }

    ::kill(pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

} // namespace

int main() {
    {
        const pid_t child = spawn_exiting_child(42);
        check("fork exited child", child > 0);

        if (child > 0) {
            const bool zombie = wait_for_zombie(child, std::chrono::seconds(5));
            check("child reaches zombie state without reaping", zombie);

            if (zombie) {
                const ProcessHandle handle = make_handle(child);
                check("is_running() returns false for zombie",
                      !ProcessManager::is_running(handle));
                check("reap_process() preserves exit code 42",
                      ProcessManager::reap_process(handle) == 42);
            } else {
                kill_and_reap(child);
            }
        }
    }

    {
        const pid_t child = spawn_running_child();
        check("fork running child", child > 0);

        if (child > 0) {
            const ProcessHandle handle = make_handle(child);
            check("is_running() returns true for running child",
                  ProcessManager::is_running(handle));
            check("reap_process() does not reap running child",
                  ProcessManager::reap_process(handle) == -1);
            kill_and_reap(child);
        }
    }

    check("is_running() rejects PID 0",
          !ProcessManager::is_running(make_handle(0)));
    check("is_running() rejects negative PID",
          !ProcessManager::is_running(make_handle(-1)));
    check("is_running() rejects non-existent PID",
          !ProcessManager::is_running(
              make_handle(std::numeric_limits<int>::max())));

    {
        ::unsetenv("LEMONADE_GPU_CLEANUP_WAIT_MS");
        const pid_t child = spawn_running_child();
        if (child > 0) {
            const ProcessHandle handle = make_handle(child);
            const auto start = std::chrono::steady_clock::now();
            ProcessManager::stop_process(handle);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            check("stop_process() waits the shipped 2 s GPU-cleanup delay by default",
                  elapsed >= std::chrono::milliseconds(1900));
            check("stop_process() leaves no running child behind",
                  !ProcessManager::is_running(handle));
        }
    }

    {
        ::setenv("LEMONADE_GPU_CLEANUP_WAIT_MS", "0", 1);
        const pid_t child = spawn_running_child();
        if (child > 0) {
            const ProcessHandle handle = make_handle(child);
            const auto start = std::chrono::steady_clock::now();
            ProcessManager::stop_process(handle);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            ::unsetenv("LEMONADE_GPU_CLEANUP_WAIT_MS");
            check("LEMONADE_GPU_CLEANUP_WAIT_MS=0 removes the GPU-cleanup delay",
                  elapsed < std::chrono::milliseconds(1000));
            check("stop_process() with no wait still leaves no running child",
                  !ProcessManager::is_running(handle));
        }
    }

    {
        ::unsetenv("LEMONADE_GPU_CLEANUP_WAIT_MS");
        const pid_t child = spawn_running_child();
        if (child > 0) {
            const ProcessHandle handle = make_handle(child);
            const auto start = std::chrono::steady_clock::now();
            ProcessManager::stop_process(handle, 0);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            check("a backend's own 0 ms wait removes the GPU-cleanup delay with nothing in the environment",
                  elapsed < std::chrono::milliseconds(1000));
            check("stop_process() with a backend wait leaves no running child behind",
                  !ProcessManager::is_running(handle));
        }
    }

    {
        ::unsetenv("LEMONADE_GPU_CLEANUP_WAIT_MS");
        const pid_t child = spawn_running_child();
        if (child > 0) {
            const ProcessHandle handle = make_handle(child);
            const auto start = std::chrono::steady_clock::now();
            ProcessManager::stop_process(handle, 250);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            check("a backend's 250 ms wait is what runs, not the 2 s default",
                  elapsed >= std::chrono::milliseconds(200) &&
                      elapsed < std::chrono::milliseconds(1200));
            check("stop_process() with a 250 ms wait leaves no running child behind",
                  !ProcessManager::is_running(handle));
        }
    }

    {
        // The operator's environment variable is their word: it wins over a backend's value.
        ::setenv("LEMONADE_GPU_CLEANUP_WAIT_MS", "0", 1);
        const pid_t child = spawn_running_child();
        if (child > 0) {
            const ProcessHandle handle = make_handle(child);
            const auto start = std::chrono::steady_clock::now();
            ProcessManager::stop_process(handle, 2000);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            ::unsetenv("LEMONADE_GPU_CLEANUP_WAIT_MS");
            check("LEMONADE_GPU_CLEANUP_WAIT_MS wins over a backend's own wait",
                  elapsed < std::chrono::milliseconds(1000));
            check("stop_process() with an env override leaves no running child behind",
                  !ProcessManager::is_running(handle));
        }
    }

    if (failures == 0) {
        std::printf("\nAll process_manager tests passed\n");
        return 0;
    }

    std::printf("\n%d process_manager test(s) failed\n", failures);
    return 1;
}
