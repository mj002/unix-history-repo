//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++98, c++03

// <queue>

// explicit priority_queue(const Compare& comp, container_type&& c);

#include <queue>
#include <cassert>

#include "MoveOnly.h"


template <class C>
C
make(int n)
{
    C c;
    for (int i = 0; i < n; ++i)
        c.push_back(MoveOnly(i));
    return c;
}


int main()
{
    std::priority_queue<MoveOnly> q(std::less<MoveOnly>(), make<std::vector<MoveOnly> >(5));
    assert(q.size() == 5);
    assert(q.top() == MoveOnly(4));
}
