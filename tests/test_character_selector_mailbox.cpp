#include <cassert>
#include "../gpu/shim/kzsf_character_selector.h"
int main() {
    KzsfCharacterSelector state;
    state.Request(1);assert(state.Take()==0);
    state.enabled=true;state.Request(1);
    const auto engineer=state.Take();assert(uint32_t(engineer)==2 && state.Take()==0);
    assert(state.Status()==2 && state.Selected()==1);
    state.Publish(engineer,0,1,7);assert(state.Status()==1 && state.Selected()==1);
    state.Request(2);const auto commando=state.Take();
    state.Publish(engineer,1,0,7);
    assert(state.Status()==2 && state.Selected()==2); // late old completion cannot dismiss the new request
    state.Publish(commando,2,0,7);assert(state.Status()==0 && state.Selected()==2);
    state.Request(0);const auto original=state.Take();state.Publish(original,2,4,7);
    assert(state.Status()==4 && state.Selected()==2); // rejected request displays the actual model
    state.Request(KZSF_CHARACTER_MODEL_COUNT);state.Request(-1);assert(state.Take()==0);
    for(int model=3;model<KZSF_CHARACTER_MODEL_COUNT;model++) {
        if(!kzsf_character_model_selectable(model))continue;
        state.Request(model);const auto token=state.Take();state.Publish(token,model,0,127);
        assert(state.Selected()==model && state.available.load()==127);
    }
    state.Request(0);auto token=state.Take();state.Publish(token,0,0,1);
    state.Cycle(1,127);token=state.Take();assert(uint32_t(token)==2);
    state.Publish(token,0,3,1);assert(state.Selected()==0); // actual appearance stays default
    state.Cycle(1,127);token=state.Take();assert(uint32_t(token)==3); // advance past failed Engineer
    state.Publish(token,0,4,1);
    state.Cycle(-1,127);token=state.Take();assert(uint32_t(token)==2);
    state.Cycle(-1,127);token=state.Take();assert(uint32_t(token)==1);
    state.Cycle(-1,127);token=state.Take();assert(uint32_t(token)==7); // wrap while still pending
    state.enabled=false;state.Reset();
    assert(state.Take()==0 && state.Status()==0 && state.Selected()==0 && state.available.load()==1);
    state.enabled=true;state.Cycle(1,127);token=state.Take();assert(uint32_t(token)==2);
    state.Reset();
    const int last=KZSF_CHARACTER_MODEL_COUNT-1;
    state.Request(13);state.Request(16);assert(state.Take()==0);
    const uint64_t high=UINT64_C(1)<<last;
    state.Cycle(-1,high);token=state.Take();assert(uint32_t(token)==uint32_t(last+1));
    state.Publish(token,last,0,high|1);assert(state.Selected()==last && state.available.load()==(high|1));
    state.Cycle(1,high);token=state.Take();assert(uint32_t(token)==1);
}
