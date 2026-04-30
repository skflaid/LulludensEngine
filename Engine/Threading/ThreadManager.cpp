#include "ThreadManager.h"

ThreadManager::ThreadManager() = default;

ThreadManager::~ThreadManager()
{
    RequestStop();
    Join();
}

void ThreadManager::Start(ThreadEntry gameEntry, ThreadEntry physicsEntry, ThreadEntry renderEntry)
{
    // Restarting is safe: first ask existing workers to exit, then wait for
    // their std::thread objects before launching new entries.
    RequestStop();
    Join();

    // Workers poll this atomic through IsRunning() or a captured running flag.
    m_Running.store(true);
    StartThread(std::move(gameEntry));
    StartThread(std::move(physicsEntry));
    StartThread(std::move(renderEntry));
}

void ThreadManager::RequestStop()
{
    // Cooperative cancellation: this does not kill threads, it only lets their
    // loops observe that they should return.
    m_Running.store(false);
}

void ThreadManager::Join()
{
    // Joining on shutdown guarantees Engine systems outlive any worker access.
    for (std::thread& thread : m_Threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_Threads.clear();
}

void ThreadManager::StartThread(ThreadEntry entry)
{
    if (entry) {
        // A null entry means that phase is driven elsewhere, not on a worker.
        m_Threads.emplace_back(std::move(entry));
    }
}
