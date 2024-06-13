#pragma once

//
// mpsc_fifo - multiple producer single consumer lock free FIFO queue
// implemented as a ring buffer above statically allocated array
// read position  (head) can be updated only by a single thread
// write position (tail) can be updated by several threads simultaneously
//

#include <cstdint>

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

static inline uint64_t interlockedCompareExchange(volatile uint64_t* p, uint64_t old_val, uint64_t new_val)
{
    // use compiler built-in intrinsic for "lock cmpxchg"
    return __sync_val_compare_and_swap(p, old_val, new_val);
}

template <class T, uint32_t CAPACITY>
class mpsc_fifo_t
{
    struct node_t
    {
        volatile uint64_t m_dataReady;
        volatile T        m_data;

        node_t() : m_dataReady(0), m_data(nullptr) {}
    };

    #pragma pack(push)
    #pragma pack(1)
    union index_t
    {
        struct
        {
            volatile uint32_t index;
            volatile uint32_t tag;
        };
        volatile uint64_t raw;

        index_t(uint64_t _raw = 0) :raw(_raw) { ; }
        operator uint64_t () { return raw; }
        operator volatile uint64_t* () { return &raw; }
        bool operator == (const index_t& _other) { return this->raw == _other.raw; }
    };
    #pragma pack(pop)

private:
    index_t   m_head;
    index_t   m_tail;
    node_t    m_nodes[CAPACITY];

    static inline index_t nextIndex(index_t index)
    {
        if (unlikely(++index.index == CAPACITY))
        {
            index.index = 0;
            index.tag++;
        }

        return index;
    }

public:
    bool     isEmpty() const { return m_head.index == m_tail.index; }
    uint32_t getCapacity() const { return CAPACITY; }

    /**
     * adds data to the tail of the queue
     * @param tail data to add to the queue
     * @return true if data was successfully added
     *         false if maximum capacity is reached
     * Note: uses reference to T (T&), so if T is not pointer type,
     *       copy constructor/assignment operator will be called
     */
    bool pushTail(const T& tail)
    {
        index_t current_tail;
        index_t new_tail;
        index_t old_tail;

        while (true)
        {
            current_tail = m_tail;
            new_tail = nextIndex(current_tail);

            if (unlikely(new_tail.index == m_head.index)) //max capacity reached
                return false;

            // the only "sync" point between producer threads.
            // try atomically change current tail with the new one
            old_tail = interlockedCompareExchange(m_tail, current_tail, new_tail);
            if (likely(old_tail == current_tail))
                break;

            //other thread updated before us, try once more
        }

        // now the new tail is visible to popHead() function, but data is still missing
        // and can't be consumed until the "ready" flag is set
        // so, write the data to the new tail and set the flag
        m_nodes[current_tail.index].m_data = tail;
        m_nodes[current_tail.index].m_dataReady = 1;

        return true;
    }

    /**
     * returns pointer to the data in the head of the queue
     * @param head address of pointer that receives head address
     * @return true if pointer was successfully copied
     *         false if queue is empty
     */
    bool peekHead(T& head)
    {
        if (unlikely(m_nodes[m_head.index].m_dataReady == 0)) //queue is empty, or data is being written to the head, but still not ready
            return false;

        head = m_nodes[m_head.index].m_data;

        return true;
    }

    /**
     * removes data from the head of the queue, assumes queue is not empty
     * Note: !!! use with peekHead() with a caution !!!
     *       only after peekHead() returned true.
     *       this function doesn't make any checks
     *       it is designed for the fast path.
     */
    void popHead()
    {
        m_nodes[m_head.index].m_dataReady = 0;
        m_head = nextIndex(m_head);
    }
};