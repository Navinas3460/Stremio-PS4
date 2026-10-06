// Threads, mutexes and condition variables.
// PS4: kernel pthreads, so the stack size can be chosen (libSceHttp fails with
// ORBIS_HTTP_ERROR_INSUFFICIENT_STACKSIZE on small stacks; 1 MiB is proven).
// Desktop: the standard library.
#pragma once

#include <cstddef>

#if defined(STREMIO_PS4)
#include <stdint.h>
#include <orbis/libkernel.h>
#else
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace threads {

#if defined(STREMIO_PS4)

class Mutex {
public:
	Mutex() { scePthreadMutexInit(&m_, nullptr, "stremio"); }
	~Mutex() { scePthreadMutexDestroy(&m_); }
	void lock() { scePthreadMutexLock(&m_); }
	void unlock() { scePthreadMutexUnlock(&m_); }
	OrbisPthreadMutex *native() { return &m_; }

private:
	OrbisPthreadMutex m_;
};

class CondVar {
public:
	CondVar() { scePthreadCondInit(&c_, nullptr, "stremio"); }
	~CondVar() { scePthreadCondDestroy(&c_); }
	void wait(Mutex &m) { scePthreadCondWait(&c_, m.native()); }
	void notify_one() { scePthreadCondSignal(&c_); }
	void notify_all() { scePthreadCondBroadcast(&c_); }

private:
	OrbisPthreadCond c_;
};

inline bool start(void *(*entry)(void *), void *arg, size_t stack_size, const char *name) {
	OrbisPthreadAttr attr;
	scePthreadAttrInit(&attr);
	scePthreadAttrSetstacksize(&attr, stack_size);
	OrbisPthread thread;
	const int rc = scePthreadCreate(&thread, &attr, entry, arg, name);
	scePthreadAttrDestroy(&attr);
	return rc == 0;
}

#else

class Mutex {
public:
	void lock() { m_.lock(); }
	void unlock() { m_.unlock(); }
	std::mutex &native() { return m_; }

private:
	std::mutex m_;
};

class CondVar {
public:
	void wait(Mutex &m) {
		std::unique_lock<std::mutex> lock(m.native(), std::adopt_lock);
		c_.wait(lock);
		lock.release(); // the caller still owns the mutex
	}
	void notify_one() { c_.notify_one(); }
	void notify_all() { c_.notify_all(); }

private:
	std::condition_variable c_;
};

inline bool start(void *(*entry)(void *), void *arg, size_t /*stack_size*/, const char * /*name*/) {
	std::thread(entry, arg).detach();
	return true;
}

#endif

class Lock {
public:
	explicit Lock(Mutex &m) : m_(m) { m_.lock(); }
	~Lock() { m_.unlock(); }

private:
	Mutex &m_;
};

} // namespace threads
