#include "hcl_unique_sorted_vector.h"

#include "hcl_api_types.h"
#include <algorithm>
#include <iterator>

const ranks_vector& UniqueSortedVector::get_vector() const
{
    return m_vec;
}

void UniqueSortedVector::assign_vector(ranks_vector vec)
{
    m_vec = vec;
}

HCL_Rank UniqueSortedVector::operator[](ranks_vector::size_type n)
{
    return m_vec[n];
}

HCL_Rank UniqueSortedVector::operator[](ranks_vector::size_type n) const
{
    return m_vec[n];
}

void UniqueSortedVector::operator=(const UniqueSortedVector& x)
{
    m_vec = x.get_vector();
}

void UniqueSortedVector::operator=(const ranks_vector& x)
{
    m_vec = x;
}

bool UniqueSortedVector::operator==(const UniqueSortedVector& rhs) const
{
    return (m_vec == rhs.get_vector());
}

bool UniqueSortedVector::operator!=(const UniqueSortedVector& rhs) const
{
    return (m_vec != rhs.get_vector());
}

bool UniqueSortedVector::operator<(const UniqueSortedVector& rhs) const
{
    return (m_vec < rhs.get_vector());
}

bool UniqueSortedVector::operator<=(const UniqueSortedVector& rhs) const
{
    return (m_vec <= rhs.get_vector());
}

bool UniqueSortedVector::operator>(const UniqueSortedVector& rhs) const
{
    return (m_vec > rhs.get_vector());
}

bool UniqueSortedVector::operator>=(const UniqueSortedVector& rhs) const
{
    return (m_vec >= rhs.get_vector());
}

const ranks_vector::size_type UniqueSortedVector::size() const
{
    return m_vec.size();
}

void UniqueSortedVector::insert_sorted(HCL_Rank new_rank)
{
    ranks_vector::iterator upper = std::upper_bound(m_vec.begin(), m_vec.end(), new_rank);

    if (upper == m_vec.end())
    {
        if (m_vec.size() == 0 || m_vec.back() < new_rank)
        {
            m_vec.push_back(new_rank);
        }
    }
    else if (upper == m_vec.begin())
    {
        m_vec.insert(upper, new_rank);
    }
    else if (*(std::prev(upper)) < new_rank)
    {
        m_vec.insert(upper, new_rank);
    }
}

void UniqueSortedVector::insert_range_sorted(ranks_vector::const_iterator start_rank_it,
                                             ranks_vector::const_iterator end_rank_it)
{
    ranks_vector::const_iterator it = start_rank_it;

    while (it != end_rank_it)
    {
        insert_sorted(*it);
        it = std::next(it);
    }
}

void UniqueSortedVector::insert(ranks_vector::iterator begin, ranks_vector::iterator end)
{
    m_vec.insert(m_vec.begin(), begin, end);
}

std::ostream& operator<<(std::ostream& os, const UniqueSortedVector& uniqueSortedVector)
{
    unsigned vecCount = 1;
    for (const HCL_Rank rank : uniqueSortedVector)
    {
        os << rank << (vecCount < uniqueSortedVector.size() ? ", " : "");
        vecCount++;
    }

    return os;
}
