#include "hcl_bits.h"
#include "hcl_utils.h"
#include <sstream>

std::string bits_t::to_str() const
{
    std::stringstream ss;
    unsigned          i = 0;

    ss << "bits(";
    for (auto bit : (*this))
    {
        if (i++ == count() - 1)
        {
            ss << bit;
        }
        else
        {
            ss << bit << ", ";
        }
    }
    ss << ")";

    return ss.str();
}

unsigned bits_t::find(unsigned Nth) const
{
    int i = Nth;
    for (auto bit_index : (*this))
    {
        if (i-- == 0) return bit_index;
    }

    VERIFY(false, "out of bounds. {} > {}", Nth, count());

    return -1;
}