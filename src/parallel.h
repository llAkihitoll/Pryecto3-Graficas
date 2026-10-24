#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Grupo fijo de hilos que se crean una sola vez y se reutilizan en cada
// cuadro (crear hilos nuevos en cada llamada saldria caro).
class ThreadPool {
public:
    explicit ThreadPool(int extraThreads);
    ~ThreadPool();

    // Llama job(i) para cada i en [0, count), repartiendo los indices entre
    // los hilos (el hilo que llama tambien trabaja) y espera a que terminen.
    void parallelFor(int count, const std::function<void(int)>& job);

    int threadCount() const { return (int)workers.size() + 1; }

private:
    void workerLoop();
    void runJobs(unsigned gen, const std::function<void(int)>* job, int count);

    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake, done;

    // Trabajo actual (protegido por mutex).
    const std::function<void(int)>* currentJob = nullptr;
    int jobCount = 0;
    int finished = 0;        // indices terminados
    unsigned generation = 0; // cambia con cada parallelFor
    bool stopping = false;

    // Siguiente indice libre. Los 32 bits altos guardan la generacion, asi
    // un hilo atrasado nunca toma un indice de un trabajo que no es el suyo.
    std::atomic<uint64_t> ticket{0};
};

// Pool compartido por todo el programa: un hilo por nucleo logico.
ThreadPool& threadPool();
