#include <eekernel.h>

#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Retail runs several threads: the movie player decodes on three of them, the loading screens draw
// on one, and the controller stepped its vibration on another. The EE ran them on its single core,
// so no two ever ran at once. The host keeps that promise: each thread is a real OS thread, but the
// scheduler lets exactly one run game code at a time and hands the "CPU" on at the kernel calls the
// game yields in (RotateThreadReadyQueue and the semaphores). A thread that is not scheduled parks
// on the scheduler's condition variable, so nothing spins, and a thread that TerminateThread kills
// throws out of its parked kernel call at its next turn, unwinding the game frame it was in.

namespace {

// A killed thread unwinds with this at its next kernel call.
struct ThreadKilled {};

struct HostSema;

struct HostThread {
    enum class State { kUnstarted, kReady, kRunning, kWaitingSema, kFinished };

    int id = 0;
    int priority = 10; // EE priorities: a lower number runs first; retail's main thread is 10.
    State state = State::kUnstarted;
    void (*entry)(void *) = nullptr;
    void *arg = nullptr;
    bool terminated = false;
    HostSema *waiting_sema = nullptr;
    int sema_result = 0;
    unsigned long long seq = 0; // Order in the ready queue among equal priorities.
    std::thread worker;
};

struct HostSema {
    int id = 0;
    int count = 0;
    int max = 0;
    std::vector<HostThread *> waiters;
};

struct Scheduler {
    std::mutex lock;
    std::condition_variable cv;
    HostThread *current = nullptr;
    std::vector<HostThread *> ready;
    std::unordered_map<int, HostThread *> threads;
    std::unordered_map<int, HostSema *> semas;
    int next_thread_id = 1;
    int next_sema_id = 0;
    unsigned long long tick = 0;
};

// Leaked on purpose: threads may still be parked in it when the run stops.
Scheduler &Sched() {
    static Scheduler *scheduler = new Scheduler;
    return *scheduler;
}

// The HostThread the calling OS thread is, as the EE sees it. The first kernel call from the boot
// thread registers it. Retail's main thread sets its own priority to MainThreadPriority (10) at
// startup; the port's driver main took that function's place, so the register does the same here.
thread_local HostThread *g_self = nullptr;

HostThread *Current() {
    if (g_self == nullptr) {
        HostThread *main = new HostThread;
        main->id = 0;
        main->priority = 10;
        main->state = HostThread::State::kRunning;
        g_self = main;
        Sched().threads[0] = main;
    }

    return g_self;
}

HostThread *FindThread(int thread_id) {
    auto &threads = Sched().threads;
    auto  it = threads.find(thread_id);
    return it == threads.end() ? nullptr : it->second;
}

void Enqueue(HostThread *thread) {
    thread->state = HostThread::State::kReady;
    thread->seq = Sched().tick++;
    Sched().ready.push_back(thread);
}

// The highest-priority ready thread, oldest first among equals.
HostThread *PickNext() {
    Scheduler &scheduler = Sched();
    HostThread *best = nullptr;
    std::size_t best_index = 0;

    for (std::size_t i = 0; i < scheduler.ready.size(); i++) {
        HostThread *thread = scheduler.ready[i];

        if (best == nullptr || thread->priority < best->priority ||
            (thread->priority == best->priority && thread->seq < best->seq)) {
            best = thread;
            best_index = i;
        }
    }

    if (best != nullptr) {
        scheduler.ready.erase(scheduler.ready.begin() + static_cast<std::ptrdiff_t>(best_index));
    }

    return best;
}

// Hands the CPU to a specific thread, which is parked (possibly already in the ready queue) and
// runs from where it stopped. PickNext does this for the ready queue; this is for a thread that
// must run out of turn (a preempting newcomer, a thread being terminated).
void Handoff(HostThread *next) {
    Scheduler &scheduler = Sched();

    std::erase(scheduler.ready, next);
    scheduler.current = next;
    next->state = HostThread::State::kRunning;
    scheduler.cv.notify_all();
}

// Hands the CPU to the next ready thread and parks the caller; returns with the CPU back.
void Park(std::unique_lock<std::mutex> &lock, HostThread *me) {
    Scheduler &scheduler = Sched();
    HostThread *next = PickNext();

    scheduler.current = next;

    if (next != nullptr) {
        next->state = HostThread::State::kRunning;
    }

    scheduler.cv.notify_all();
    scheduler.cv.wait(lock, [&] { return scheduler.current == me; });
    me->state = HostThread::State::kRunning;
}

void FinishSelf(HostThread *self) {
    Scheduler &scheduler = Sched();
    std::unique_lock lock(scheduler.lock);

    std::erase(scheduler.ready, self);
    self->state = HostThread::State::kFinished;
    scheduler.cv.notify_all();

    if (scheduler.current == self) {
        HostThread *next = PickNext();
        scheduler.current = next;

        if (next != nullptr) {
            next->state = HostThread::State::kRunning;
        }

        scheduler.cv.notify_all();
    }
}

// Every emulated thread's body: wait for the first grant of the CPU, run the game's entry, then
// leave. A terminated thread skips the entry and unwinds instead.
void ThreadEntry(HostThread *self) {
    g_self = self;

    Scheduler &scheduler = Sched();
    std::unique_lock lock(scheduler.lock);

    scheduler.cv.wait(lock, [&] { return scheduler.current == self; });

    bool killed = self->terminated;

    if (!killed) {
        self->state = HostThread::State::kRunning;
    }

    lock.unlock();

    if (!killed) {
        try {
            self->entry(self->arg);
        } catch (...) {
            // TerminateThread arrives as an exception at the victim's next kernel call; the game
            // frame unwinds through here and the thread is done.
        }
    }

    FinishSelf(self);
}

} // namespace

extern "C" {

int CreateThread(struct ThreadParam *param) {
    if (param == nullptr || param->entry == nullptr) {
        return -1;
    }

    std::unique_lock lock(Sched().lock);

    HostThread *thread = new HostThread;
    thread->id = Sched().next_thread_id++;
    thread->priority = param->initPriority;
    thread->entry = param->entry;
    Sched().threads[thread->id] = thread;

    return thread->id;
}

int StartThread(int thread_id, void *arg) {
    std::unique_lock lock(Sched().lock);

    HostThread *me = Current();
    HostThread *thread = FindThread(thread_id);

    if (thread == nullptr || thread->state != HostThread::State::kUnstarted) {
        return -1;
    }

    thread->arg = arg;
    Enqueue(thread);
    thread->worker = std::thread(ThreadEntry, thread);

    // A newcomer that outranks the caller takes the CPU right away, as on the EE.
    if (thread->priority < me->priority) {
        Enqueue(me);
        Handoff(thread);
        Sched().cv.wait(lock, [&] { return Sched().current == me; });
        me->state = HostThread::State::kRunning;
    }

    return 0;
}

int RotateThreadReadyQueue(int priority) {
    (void) priority;

    std::unique_lock lock(Sched().lock);

    HostThread *me = Current();

    if (me->terminated) {
        lock.unlock();
        throw ThreadKilled{};
    }

    Enqueue(me);
    HostThread *next = PickNext();

    if (next == nullptr || next == me) {
        // Alone in the ready queue: the EE reschedules the caller and it continues.
        me->state = HostThread::State::kRunning;
        lock.unlock();
        return 0;
    }

    Sched().current = next;
    next->state = HostThread::State::kRunning;
    Sched().cv.notify_all();
    Sched().cv.wait(lock, [&] { return Sched().current == me; });
    me->state = HostThread::State::kRunning;

    bool killed = me->terminated;

    lock.unlock();

    if (killed) {
        throw ThreadKilled{};
    }

    return 0;
}

int TerminateThread(int thread_id) {
    std::unique_lock lock(Sched().lock);

    HostThread *me = Current();

    if (thread_id == me->id) {
        me->terminated = true;
        lock.unlock();
        throw ThreadKilled{};
    }

    HostThread *thread = FindThread(thread_id);

    if (thread == nullptr) {
        return -1;
    }

    if (thread->state == HostThread::State::kFinished || thread->state == HostThread::State::kUnstarted) {
        thread->state = HostThread::State::kFinished;
        return 0;
    }

    thread->terminated = true;

    if (thread->state == HostThread::State::kWaitingSema) {
        HostSema *sema = thread->waiting_sema;
        std::erase(sema->waiters, thread);
        thread->waiting_sema = nullptr;
        thread->sema_result = -1;
        Enqueue(thread);
    }

    // Run the victim to its end so DeleteThread never frees a live thread. Its kernel call it is
    // parked in sees terminated when it next gets the CPU and unwinds from there.
    while (thread->state != HostThread::State::kFinished) {
        Enqueue(me);
        Handoff(thread);
        Sched().cv.wait(lock, [&] { return Sched().current == me; });
        me->state = HostThread::State::kRunning;
    }

    return 0;
}

int DeleteThread(int thread_id) {
    std::unique_lock lock(Sched().lock);

    HostThread *thread = FindThread(thread_id);

    if (thread == nullptr) {
        return -1;
    }

    if (thread->state != HostThread::State::kFinished) {
        lock.unlock();
        TerminateThread(thread_id);
        lock.lock();
    }

    std::thread worker = std::move(thread->worker);
    Sched().threads.erase(thread_id);
    lock.unlock();

    if (worker.joinable()) {
        worker.join();
    }

    delete thread;
    return 0;
}

int GetThreadId(void) {
    std::unique_lock lock(Sched().lock);
    return Current()->id;
}

int ChangeThreadPriority(int thread_id, int priority) {
    std::unique_lock lock(Sched().lock);

    HostThread *thread = FindThread(thread_id);

    if (thread == nullptr) {
        return -1;
    }

    thread->priority = priority;
    return 0;
}

int CreateSema(struct SemaParam *param) {
    std::unique_lock lock(Sched().lock);

    HostSema *sema = new HostSema;
    sema->id = Sched().next_sema_id++;

    if (param != nullptr) {
        sema->count = param->initCount;
        sema->max = param->maxCount;
        param->currentCount = sema->count;
    }

    Sched().semas[sema->id] = sema;
    return sema->id;
}

int DeleteSema(int sema_id) {
    std::unique_lock lock(Sched().lock);

    auto it = Sched().semas.find(sema_id);

    if (it == Sched().semas.end()) {
        return -1;
    }

    HostSema *sema = it->second;
    Sched().semas.erase(it);

    // Waiters that were released by the deletion carry an error when they come back.
    for (HostThread *waiter : sema->waiters) {
        waiter->waiting_sema = nullptr;
        waiter->sema_result = -1;
        Enqueue(waiter);
    }

    sema->waiters.clear();
    return 0;
}

int WaitSema(int sema_id) {
    std::unique_lock lock(Sched().lock);

    HostThread *me = Current();
    auto it = Sched().semas.find(sema_id);

    if (it == Sched().semas.end()) {
        return -1;
    }

    HostSema *sema = it->second;

    if (sema->count > 0) {
        sema->count--;
        return 0;
    }

    me->waiting_sema = sema;
    me->sema_result = 0;
    sema->waiters.push_back(me);

    Park(lock, me);

    me->waiting_sema = nullptr;
    bool killed = me->terminated;
    int  result = me->sema_result;

    lock.unlock();

    if (killed) {
        throw ThreadKilled{};
    }

    return result;
}

int SignalSema(int sema_id) {
    std::unique_lock lock(Sched().lock);

    HostThread *me = Current();
    auto it = Sched().semas.find(sema_id);

    if (it == Sched().semas.end()) {
        return -1;
    }

    HostSema *sema = it->second;
    HostThread *waiter = nullptr;

    while (!sema->waiters.empty()) {
        HostThread *candidate = sema->waiters.front();
        sema->waiters.erase(sema->waiters.begin());

        if (!candidate->terminated) {
            waiter = candidate;
            break;
        }
    }

    if (waiter != nullptr) {
        // The token passes to the waiter; the count does not change.
        waiter->waiting_sema = nullptr;
        waiter->sema_result = 0;
        Enqueue(waiter);

        if (waiter->priority < me->priority) {
            Enqueue(me);
            Park(lock, me);
        }
    } else if (sema->count < sema->max) {
        sema->count++;
    }

    return 0;
}

// The EE's Exit kills every thread at once; the host exits the process the same way.
void Exit__2(int status) {
    Exit(status);
}

// The game puts this in ThreadParam::gpReg; no host thread needs a global pointer register.
void *_gp = nullptr;

} // extern "C"
