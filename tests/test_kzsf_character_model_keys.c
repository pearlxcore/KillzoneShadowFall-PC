#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_character_models.h"
int main(void) {
    KzsfCharacterModelKeys keys={0};
    assert(kzsf_character_next_loaded(0,1,9)==3);
    assert(kzsf_character_next_loaded(3,1,9)==0);
    assert(kzsf_character_next_loaded(0,-1,9)==3);
    assert(kzsf_character_next_loaded(0,1,0)==0);
    assert(kzsf_character_next_loaded(6,1,127)==0);
    assert(kzsf_character_next_loaded(0,-1,127)==6);
    const int last=KZSF_CHARACTER_MODEL_COUNT-1;
    const uint64_t high=UINT64_C(1)<<last;
    assert(kzsf_character_model_mask()&high);
    assert(kzsf_character_next_loaded(0,-1,high)==last);
    assert(kzsf_character_next_loaded(last,1,high)==0);
    assert(kzsf_character_model_index("ISA_Lamb_SpaceSuit")==last);
    assert(kzsf_character_model_index("ISA_Lamb_Prisoner")>=0);
    assert(kzsf_character_model_index("ISA_Lamb_Civilian_Disguise")>=0);
    assert(kzsf_character_model_index("VSA_Massar")==-1);
    for(int i=1;i<=15;i++) {
        char name[64];snprintf(name,sizeof(name),"vsa_Civilians_Female_%02d",i);
        assert(kzsf_character_model_index(name)==-1);
    }
    for(int a=0;a<KZSF_CHARACTER_MODEL_COUNT;a++)for(int b=a+1;b<KZSF_CHARACTER_MODEL_COUNT;b++)
        assert(strcmp(kzsf_character_model_name(a),kzsf_character_model_name(b)));
    for(int model=1;model<KZSF_CHARACTER_MODEL_COUNT;model++)assert(*kzsf_character_model_bundle(model));
    assert(!kzsf_character_model_keys(&keys,1,1,0,0));
    assert(kzsf_character_model_keys(&keys,1,1,0,1)==1);
    assert(!kzsf_character_model_keys(&keys,1,1,0,1));
    assert(!kzsf_character_model_keys(&keys,1,0,0,0));
    assert(!kzsf_character_model_keys(&keys,1,0,0,1));
    assert(kzsf_character_model_keys(&keys,1,1,0,1)==1);
    assert(!kzsf_character_model_keys(&keys,1,0,0,0));
    assert(!kzsf_character_model_keys(&keys,1,1,0,0));
    assert(kzsf_character_model_keys(&keys,1,0,0,0)==2);
    assert(kzsf_character_model_keys(&keys,1,1,1,0)==-1);
    assert(!kzsf_character_model_keys(&keys,1,1,0,0));
    assert(kzsf_character_model_keys(&keys,1,1,0,1)==1);
    assert(!kzsf_character_model_keys(&keys,0,0,0,0));
    assert(!kzsf_character_model_keys(&keys,1,0,0,0));
    for(int i=0;i<KZSF_CHARACTER_MODEL_COUNT;i++)assert(kzsf_character_model_index(kzsf_character_model_name(i))==i);
}
