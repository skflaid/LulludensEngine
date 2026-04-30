#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class ThreadManager {
public:
    using ThreadEntry = std::function<void()>;

    ThreadManager();
    ~ThreadManager();

    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;

    // Starts the runtime entries that are non-null. Today the game entry can be
    // null because the game tick is still owned by the platform main loop.
    void Start(ThreadEntry gameEntry, ThreadEntry physicsEntry, ThreadEntry renderEntry);
    // Signals worker loops to leave; Join() performs the actual wait.
    void RequestStop();
    void Join();

    bool IsRunning() const { return m_Running.load(); }

private:
    void StartThread(ThreadEntry entry);

private:
    // Shared loop flag read by every worker thread.
    std::atomic_bool m_Running = false;
    // The actual OS threads. They are always joined before being cleared.
    std::vector<std::thread> m_Threads;
};
