#include <boost/container/static_vector.hpp>
#include <boost/container/throw_exception.hpp>
#include <cassert>
#include <cstdio>
#include <stdexcept>
int main() {
    bool caught=false;
    try {
        boost::container::static_vector<int,1> values;
        values.push_back(1);
        values.push_back(2); // Capacity failure, even with ample RAM.
    } catch (const boost::container::bad_alloc&) { caught=true; }
    assert(caught);
    try { throw std::runtime_error("ordinary exception"); }
    catch (const std::runtime_error&) { puts("allocation and ordinary exception semantics preserved"); }
}
