#include "parallel.h"
#include <algorithm>

ThreadPool::ThreadPool(int extraThreads) {
    for (int i = 0; i < extraThreads; ++i) workers.emplace_back([this]() { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    for (std::thread& w : workers) w.join();
}

// Toma indices libres hasta que se acaben: cada hilo agarra el siguiente
// disponible, asi los que terminan rapido ayudan con lo que falta.
void ThreadPool::runJobs(unsigned gen, const std::function<void(int)>* job, int count) {
    int doneHere = 0;
    uint64_t t = ticket.load();
    while ((unsigned)(t >> 32) == gen && (int)(uint32_t)t < count) {
        // Solo se queda con el indice si nadie mas lo tomo antes.
        if (ticket.compare_exchange_weak(t, t + 1)) {
            (*job)((int)(uint32_t)t);
            doneHere++;
            t = ticket.load();
        }
    }
    if (doneHere > 0) {
        std::lock_guard<std::mutex> lock(mutex);
        finished += doneHere;
        if (finished == count) done.notify_all();
    }
}

void ThreadPool::workerLoop() {
    unsigned seen = 0;
    while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [&]() { return stopping || generation != seen; });
        if (stopping) return;
        seen = generation;
        const std::function<void(int)>* job = currentJob;
        int count = jobCount;
        lock.unlock();
        runJobs(seen, job, count);
    }
}

void ThreadPool::parallelFor(int count, const std::function<void(int)>& job) {
    if (count <= 0) return;
    unsigned gen;
    {
        std::lock_guard<std::mutex> lock(mutex);
        gen = ++generation;
        currentJob = &job;
        jobCount = count;
        finished = 0;
        ticket = (uint64_t)gen << 32;
    }
    wake.notify_all();
    runJobs(gen, &job, count);
    std::unique_lock<std::mutex> lock(mutex);
    done.wait(lock, [&]() { return finished == count; });
}

ThreadPool& threadPool() {
    static ThreadPool pool(std::max(1, (int)std::thread::hardware_concurrency() - 1));
    return pool;
}
