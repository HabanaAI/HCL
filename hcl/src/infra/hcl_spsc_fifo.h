#pragma once

//
// spsc_fifo - single producer single consumer lock free FIFO queue
// implemented as a ring buffer above statically allocated array
// Consumer Index (ci) and Producer Index (pi) can each be updated only by a single thread
// since both pointers are only modified in a single thread, locklessness can be achieved easily
//

#include <array>
#include <type_traits>
#include <sstream>

#include <hcl_utils.h>
#include "hcl_log_manager.h"  // for LOG_*

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

/**
 * Implementation of a lock-free Single Producer, Single Consumer FIFO queue, with possibly-continuous elements.
 *
 * The continuous support is good for allowing a serializer to serialize a variable-number of dwords to a cyclic
 * buffer. Example: the user wants to write a 3-dwords edma command to the scheduler. If the serialized command is
 * cut because the producer wraps-around, the data is no good.
 *
 * Locklessness is achieved by the fact that all the iterators used are only written by a single thread - the 'pi'
 * variables are written by the producer thread and the 'ci' by the consumer thread. The other thread can (and should)
 * read the data of the other thread but as long as no modification is made to the cross-thread variables coherency is
 * achieved. A caveat is that a thread may look at another's 'old' data (but when it re-loops on it the data will be
 * 'new'), so there might be a small latency cost.
 *
 * WARNING: Using multiple threads to produce or to consume will result in undefined behaviour.
 */

template<uint64_t CAPACITY>
class spsc_fifo_t
{
public:
    spsc_fifo_t(const std::string name = "NoName") : m_name(name)
    {
        VERIFY(CAPACITY >= 2, "the spsc fifo is not large enough");
        VERIFY((CAPACITY & (CAPACITY - 1)) == 0, "spsc's size must be a power of 2");

        m_ci = 0;

        m_pi        = 0;
        m_next_pi   = 0;
        m_watermark = 0;
    }

    virtual ~spsc_fifo_t() = default;

    inline uint64_t getCi()
    {
        static const uint64_t shift = std::log2(CAPACITY);
        static const uint64_t mask  = ((1 << shift) - 1);

        return m_ci & mask;
    }

    inline uint64_t getPi()
    {
        static const uint64_t shift = std::log2(CAPACITY);
        static const uint64_t mask  = ((1 << shift) - 1);

        return m_pi & mask;
    }

    inline uint64_t getNextPi()
    {
        static const uint64_t shift = std::log2(CAPACITY);
        static const uint64_t mask  = ((1 << shift) - 1);

        return m_next_pi & mask;
    }

    inline uint64_t getWatermark()
    {
        static const uint64_t shift = std::log2(CAPACITY);
        static const uint64_t mask  = ((1 << shift) - 1);

        return m_watermark & mask;
    }

    inline constexpr uint32_t getCapacity() const { return CAPACITY; }

    inline std::array<uint32_t, CAPACITY>& getBuf() { return m_buf; };

    inline bool isEmpty() { return m_ci >= m_pi; }

    inline bool isFull() { return getCi() == getPi() && !isEmpty(); }

    inline uint32_t* getNextPtr(uint64_t sizeInDwords)
    {
        VERIFY(likely(sizeInDwords <= CAPACITY));

        while (isFull())
        {
            if (unlikely(LOG_LEVEL_AT_LEAST_WARN(HCL)))
            {
                LOG_WARN_RATELIMITTER(HCL,
                                      1000,  // msec
                                      "FIFO is still full, name={}",
                                      m_name);
            }
        }

        uint32_t* ret = &m_buf[getPi()];
        if (getPi() >= getCi())
        {
            // m_pi is ahead of m_ci, no worries we overtake it. Check if we have continuous room till the end.
            if (sizeInDwords > (CAPACITY - getPi()))
            {
                // We don't have continuous room to write 'sizeInDwords' elements, so we need to wrap-around back to the
                // start of the buffer. When we do, it's possible that the producer (this thread) is too far ahead of
                // the consumer (ci) - so we wait until we're more or less aligned.
                // However, if, for example, the producer wrote CAPACITY elements and is now wrapping
                // around, but the consumer didn't read anything yet. If we don't wait here, the producer will just
                // keep writing.
                m_watermark = m_pi;
                m_next_pi += (CAPACITY - getPi());
                ret = &m_buf[getNextPi()];

                while (m_next_pi + sizeInDwords - m_ci >= CAPACITY)
                {
                }
            }
        }

        if (getPi() < getCi())
        {
            // So m_pi is behind m_ci, meaning the producer wrapped around and the consumer is catching up.
            // We must wait until we have enough room to write (the space between m_pi and m_ci is big enough).
            if (getCi() - getPi() <= sizeInDwords)
            {
                while (!isEmpty())
                {
                }
            }

            if (sizeInDwords > CAPACITY - getPi())
            {
                // If we reached here - it means that while the consumer caught up - we still don't have enough space
                // to write 'sizeInDwords' continuous elements, so we must wrap around.
                m_watermark = m_pi;
                m_next_pi += (CAPACITY - getPi());
                ret = &m_buf[getNextPi()];
            }
        }

        // Finally we are good - return the current 'pi' pointer to the user and make sure to self-mark the next pi
        // (for submit).
        m_next_pi += sizeInDwords;
        return ret;
    }

    inline void submit(bool force = false)
    {
        // submit() is called when the user has done writing and the data should be 'submitted' (i.e. read) by the
        // consumer.
        m_pi = m_next_pi;
    }

    inline uint32_t* read(uint64_t* sizeInDwords)
    {
        uint32_t* ret = &m_buf[getCi()];

        // We need the make sure the consumer didn't overtake the producer (and if we did - wait for the producer to
        // catch up. This can happen if, for example, the producer wrote CAPACITY elements and the consumer read all
        // of these elements. When the consumer free()s the data, it will wrap-around, but the producer will only
        // wrap-around when new elements want to be written.
        if (isEmpty())
        {
            *sizeInDwords = 0;
            return ret;
        }

        if (unlikely(m_ci <= m_watermark && m_watermark <= m_pi && m_watermark > 0))
        {
            // m_ci is at m_watermark, need to wrap-around and read until m_pi
            if (m_ci == m_watermark && m_watermark < m_pi)
            {
                m_ci += CAPACITY - getCi();
                *sizeInDwords = m_pi - m_ci;
                ret           = &m_buf[getCi()];
            }
            // m_ci is ahead of m_pi, so read until the watermark.
            else
            {
                *sizeInDwords = getWatermark() - getCi();
            }
        }
        else
        {
            // m_ci is behind m_pi, so read until m_pi.
            *sizeInDwords = m_pi - m_ci;
            if (m_ci <= m_watermark && m_watermark <= m_pi && m_watermark > 0)
            {
                *sizeInDwords = getWatermark() - getCi();
            }
            else if (*sizeInDwords > (CAPACITY - getCi()))
            {
                *sizeInDwords = CAPACITY - getCi();
            }
        }

        return ret;
    }

    inline void free(uint64_t sizeInDwords)
    {
        VERIFY(likely(sizeInDwords <= CAPACITY), "sizeInDwords: {} > CAP: {}", sizeInDwords, CAPACITY);

        // free() 'sizeInDwords' elements, i.e. signify that we're done with consuming this information.
        m_ci += sizeInDwords;
        if (m_ci == m_watermark && m_watermark > 0)
        {
            m_ci += (CAPACITY - getCi());
        }
    }

private:
    const std::string              m_name;
    std::array<uint32_t, CAPACITY> m_buf;

    volatile uint64_t m_ci;

    volatile uint64_t m_pi;
    volatile uint64_t m_next_pi;    // data is now being written, from m_pi to m_next_pi. on submit(), m_pi = m_next_pi
    volatile uint64_t m_watermark;  // signifies the end of continuous data until which the consumer should read.
};
