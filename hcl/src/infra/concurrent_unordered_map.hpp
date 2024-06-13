#include <unordered_map>
#include <shared_mutex>
#include <initializer_list>
#include <mutex>

template<typename Key, typename Value>
class ConcurrentUnorderedMap
{
public:
    // Default constructor
    ConcurrentUnorderedMap() = default;

    // Copy constructor
    ConcurrentUnorderedMap(const ConcurrentUnorderedMap& other)
    {
        std::shared_lock<std::shared_mutex> lock(other.m_mutex);
        m_unorderedMap = other.m_unorderedMap;
    }

    // Assignment operator
    ConcurrentUnorderedMap& operator=(const ConcurrentUnorderedMap& other)
    {
        if (this == &other)
        {
            return *this;
        }

        std::shared_lock<std::shared_mutex> selfLock(m_mutex, std::defer_lock);
        std::shared_lock<std::shared_mutex> otherLock(other.m_mutex, std::defer_lock);
        std::lock(selfLock, otherLock);

        m_unorderedMap = other.m_unorderedMap;

        return *this;
    }

    // Nested iterator class
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = std::pair<const Key, Value>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = value_type*;
        using reference         = value_type&;

        Iterator(typename std::unordered_map<Key, Value>::iterator it, std::shared_mutex& mutex)
        : it_(it), mutex_(mutex)
        {
        }

        Iterator& operator++()
        {
            std::lock_guard<std::shared_mutex> lock(mutex_);
            ++it_;
            return *this;
        }

        Iterator operator++(int)
        {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        reference operator*() const
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return *it_;
        }

        pointer operator->() const
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return &(*it_);
        }

        bool operator==(const Iterator& other) const { return it_ == other.it_; }

        bool operator!=(const Iterator& other) const { return !(*this == other); }

    private:
        typename std::unordered_map<Key, Value>::iterator it_;
        std::shared_mutex&                                mutex_;
    };

    using iterator = Iterator;

    // Rest of the class implementation...

    iterator begin()
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return iterator(m_unorderedMap.begin(), m_mutex);
    }

    iterator end()
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return iterator(m_unorderedMap.end(), m_mutex);
    }

    Value& operator[](const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        return m_unorderedMap[key];
    }

    Value get(const Key& key) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto                                it = m_unorderedMap.find(key);
        return (it != m_unorderedMap.end()) ? it->second : Value {};
    }

    void set(const Key& key, const Value& value)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_unorderedMap[key] = value;
    }

    void erase(const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_unorderedMap.erase(key);
    }

    bool contains(const Key& key) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_unorderedMap.count(key) > 0;
    }

private:
    std::unordered_map<Key, Value> m_unorderedMap;
    mutable std::shared_mutex      m_mutex;
};
