#pragma once

#include <cstdint>
#include <set>
#include <string>

class bits_t
{
    class iterator  // "for" loop support
    {
        constexpr static int START_ITR = 0;
        constexpr static int END_ITR   = -1;
        constexpr static int MAX_IDX   = 63;

    private:
        const uint64_t value_    = 0;
        int            position_ = START_ITR;

        void next_bit(int next = 1)
        {
            uint64_t mask = (position_ == MAX_IDX) ? 0 : value_ & ((~0ULL) << (position_ + next));

            // Returns one plus the index of the least significant 1-bit of x, or if x is zero, returns zero.
            position_ = __builtin_ffsll(mask) - 1;
        }

    public:
        iterator(uint64_t _value, bool begin) : value_(_value), position_(begin ? START_ITR : END_ITR)
        {
            if (begin) next_bit(0);
        }
        iterator& operator++()
        {
            next_bit();
            return *this;
        }
        uint32_t operator*() const { return position_; }
        bool     operator==(const iterator& other) const { return (position_ == other.position_); }
        bool     operator!=(const iterator& other) const { return !(*this == other); }
    };

    class bit_ref  // single bit write access: bits[8]=true
    {
    private:
        bits_t&  bits_;
        unsigned pos_;

    public:
        bit_ref(bits_t& _bits, unsigned _pos) : bits_(_bits), pos_(_pos) {}
        bit_ref& operator=(bool _x)
        {
            bits_.set(pos_, _x);
            return *this;
        }
        operator bool() const { return bits_.get(pos_); }
    };

protected:
    uint64_t raw_ = 0;

protected:
    using init_list = const std::initializer_list<unsigned>&;
    using uint_set  = const std::set<unsigned>&;

#define _INIT_                                                                                                         \
    raw_ = 0;                                                                                                          \
    for (auto b : bits)                                                                                                \
        set(b);
    void init(init_list bits) { _INIT_ }
    void init(uint_set bits) { _INIT_ }

    unsigned find(unsigned Nth) const;

public:
    bits_t(uint64_t val = 0) : raw_(val) {}
    bits_t(init_list bits) { init(bits); }  // bits_t bits{0,5,63}; bits = {1,34,45,23,61}

    bits_t& operator=(uint64_t val)
    {
        raw_ = val;
        return *this;
    }
    bits_t& operator=(init_list bits)
    {
        init(bits);
        return *this;
    }
    bits_t& operator=(uint_set bits)
    {
        init(bits);
        return *this;
    }

    operator uint64_t() const { return raw_; }

    void set(unsigned bit, bool on = true) { on ? (void)(raw_ |= (1ULL << bit)) : clear(bit); }
    bool get(unsigned bit) const { return ((raw_ >> bit) & 1ULL); }
    void clear(unsigned bit) { raw_ &= ~(1ULL << bit); }

    // count of "on" bits
    unsigned count() const { return __builtin_popcountll(raw_); }

    // single bit read/write access
    bool    operator[](unsigned bit) const { return get(bit); }
    bit_ref operator[](unsigned bit) { return bit_ref(*this, bit); }

    // index of Nth "on" bit
    unsigned operator()(unsigned Nth) const { return find(Nth); }

    // a/l operators
    bits_t& operator&=(uint64_t val)
    {
        raw_ &= val;
        return *this;
    }
    bits_t& operator|=(uint64_t val)
    {
        raw_ |= val;
        return *this;
    }

    // for(auto bit_index : bits) --> iterate over "on" bits.
    iterator begin() const { return iterator(raw_, true); }
    iterator end() const { return iterator(raw_, false); }

    // misc
    std::string to_str() const;
};

// set first N bits to 1
#define NBITS(N) ((1ULL << (N)) - 1)

using nics_mask_t = bits_t;