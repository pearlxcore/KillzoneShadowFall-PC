#include <cassert>
#include <cmath>
#include <limits>
#include "../gpu/shim/kzsf_mouse_input.h"
int main() {
    KillzoneFreeLook a,b;
    float x,y,bx,by;
    a.Motion(100,40);a.Take(x,y,1,1,1,false);
    assert(std::abs(x-.25f)<.00001f && std::abs(y+.1f)<.00001f);
    a.Take(bx,by,1,1,1,false);assert(bx==0 && by==0); // consumed exactly once
    for(int i=0;i<10;++i)b.Motion(10,4);
    b.Take(bx,by,1,1,1,false);assert(x==bx && y==by); // event grouping independent
    a.Motion(100,40);a.Take(x,y,2,.5f,2,true);
    assert(std::abs(x-.25f)<.00001f && std::abs(y-.4f)<.00001f);
    a.Motion(std::numeric_limits<float>::quiet_NaN(),10);
    a.Take(x,y,1,1,1,false);assert(!x && !y);
    a.Motion(100,100);a.Clear();a.Take(x,y,1,1,1,false);assert(!x && !y);
    a.Motion(1e30f,-1e30f);a.Take(x,y,1,1,1,false);
    assert(std::isfinite(x) && std::isfinite(y) && std::abs(x)<=20.48f);
}
