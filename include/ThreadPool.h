#pragma once

#include <future>
#include <queue>

class ThreadPool
{
public:
    explicit ThreadPool(int threads = 0);
    virtual ~ThreadPool();
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        ->std::future<typename std::invoke_result<F,Args...>::type>;

public:
    static int get_thread_count()
    {
        auto threads = std::thread::hardware_concurrency();
        return threads ? static_cast<int>(threads) : 1;
    }

private:
    bool _stop;
    std::mutex _mutex{};
    std::condition_variable _cond{};

    std::vector<std::thread> _threads{};
    std::queue<std::function<void()>> _tasks{};
};

inline ThreadPool::ThreadPool(int threads)
    : _stop(false)
{
    if (threads <= 0)
        threads = get_thread_count();

    for (auto i = 0; i < threads; ++i)
    {
        _threads.emplace_back([this]() {
            for (;;)
            {
                std::function<void()> task;

                std::unique_lock<std::mutex> lock(_mutex);
                _cond.wait(lock, [this] { return _stop || !_tasks.empty(); });

                if (_stop && _tasks.empty())
                    break;

                task = std::move(_tasks.front());
                _tasks.pop();
                lock.unlock();

                task();
            }
            });
    }
}

inline ThreadPool::~ThreadPool()
{
    std::unique_lock<std::mutex> lock(_mutex);
    _stop = true;
    lock.unlock();

    _cond.notify_all();

    for (auto& worker : _threads)
        worker.join();
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::invoke_result<F,Args...>::type>
{
    using ret_type = typename std::invoke_result<F,Args...>::type;

    auto task = std::make_shared<std::packaged_task<ret_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

    std::future<ret_type> res = task->get_future();

    std::unique_lock<std::mutex> lock(_mutex);
    if (!_stop)
        _tasks.emplace([task]() { (*task)(); });
    lock.unlock();
    _cond.notify_one();

    return res;
}
