#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

// Fixed-size thread pool with a shared task queue. Callers submit callable
// objects and get back a std::future for the result.  The pool is safe to use
// from the calling thread and from within tasks, but tasks that wait on other
// tasks submitted to the same pool must ensure at least one idle worker
// remains available (the main thread satisfies this when it drives traffic
// while services runs as a pool task).
class ThreadPool {
public:
  explicit ThreadPool(unsigned int nThreads) {
    workers_.reserve(nThreads);
    for (unsigned int i = 0; i < nThreads; ++i) {
      workers_.emplace_back([this] {
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
          }
          task();
        }
      });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& t : workers_) t.join();
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  unsigned int threadCount() const {
    return static_cast<unsigned int>(workers_.size());
  }

  template <typename F>
  auto submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
    using R = std::invoke_result_t<std::decay_t<F>>;
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    std::future<R> future = task->get_future();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (stop_) throw std::runtime_error("ThreadPool is stopped");
      queue_.push([task]() { (*task)(); });
    }
    cv_.notify_one();
    return future;
  }

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};
