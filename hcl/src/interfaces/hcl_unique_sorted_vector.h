#pragma once

#include "hcl_api_types.h"

#include <vector>
#include <ostream>

typedef std::vector<HCL_Rank> ranks_vector;

class UniqueSortedVector
{
public:
    UniqueSortedVector() = default;

    const ranks_vector& get_vector() const;
    void                assign_vector(ranks_vector vec);

    HCL_Rank operator[](ranks_vector::size_type n);
    HCL_Rank operator[](ranks_vector::size_type n) const;
    void     operator=(const UniqueSortedVector& x);
    void     operator=(const ranks_vector& x);
    bool     operator==(const UniqueSortedVector& rhs) const;
    bool     operator!=(const UniqueSortedVector& rhs) const;
    bool     operator<(const UniqueSortedVector& rhs) const;
    bool     operator<=(const UniqueSortedVector& rhs) const;
    bool     operator>(const UniqueSortedVector& rhs) const;
    bool     operator>=(const UniqueSortedVector& rhs) const;

    ranks_vector::iterator begin() { return m_vec.begin(); };
    ranks_vector::iterator end() { return m_vec.end(); };

    ranks_vector::const_iterator begin() const { return m_vec.begin(); };
    ranks_vector::const_iterator end() const { return m_vec.end(); };

    ranks_vector::const_iterator cbegin() const { return m_vec.cbegin(); };
    ranks_vector::const_iterator cend() const { return m_vec.cend(); };

    const ranks_vector::size_type size() const;

    void insert_sorted(HCL_Rank new_rank);
    void insert_range_sorted(ranks_vector::const_iterator start_rank_it, ranks_vector::const_iterator end_rank_it);
    void insert(ranks_vector::iterator begin, ranks_vector::iterator end);

private:
    ranks_vector m_vec;
};

std::ostream& operator<<(std::ostream& os, const UniqueSortedVector& uniqueSortedVector);
