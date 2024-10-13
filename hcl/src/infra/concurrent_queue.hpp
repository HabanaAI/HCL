#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class ConcurrentQueue
{
public:
    ConcurrentQueue() = default;

    ConcurrentQueue(const ConcurrentQueue&)            = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    void push(const T& item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        condition_.notify_one();
    }

    void push(T&& item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        condition_.notify_one();
    }

    template<typename... Args>
    void emplace(Args&&... args)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace(std::forward<Args>(args)...);
        condition_.notify_one();
    }

    bool pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !queue_.empty(); });
        if (queue_.empty())
        {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !queue_.empty(); });
        if (queue_.empty())
        {
            return false;
        }
        queue_.pop();
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    T& front()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.front();
    }

    const T& front() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.front();
    }

    T& back()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.back();
    }

    const T& back() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.back();
    }

    void swap(ConcurrentQueue& other)
    {
        if (this == &other)
        {
            return;
        }
        std::lock_guard<std::mutex> lock1(mutex_);
        std::lock_guard<std::mutex> lock2(other.mutex_);
        queue_.swap(other.queue_);
    }

private:
    std::queue<T>           queue_;
    mutable std::mutex      mutex_;
    std::condition_variable condition_;
};
