#define main mesh_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main
#include "../src/kzsf_character_draw_proxy.h"
int main(void) {
    KzsfCharacterDrawPart visibility[KZSF_CHARACTER_MAX_DRAW_PARTS]={{.resource=10,.mesh=100},{.resource=20,.mesh=200},
        {.resource=20,.mesh=200},{.resource=30,.mesh=300},{.resource=20,.mesh=201}};
    for(int i=0;i<5;i++)visibility[i].tagged=1000+(uintptr_t)i*16+2;
    uintptr_t batch[KZSF_CHARACTER_MAX_DRAW_PARTS];int32_t visible=-1;
    assert(kzsf_character_draw_native_batch(visibility,5,10,20,batch,&visible));
    assert(visible==5);
    for(int i=0;i<5;i++)assert(batch[i]==visibility[i].tagged);
    /* Fall transitions re-add original bodies: processing them is mandatory
     * even when their visual result is unwanted. Repeated selected meshes
     * can prepare distinct downstream jobs and must also remain ordered. */
    assert(!kzsf_character_draw_native_batch(visibility,KZSF_CHARACTER_MAX_DRAW_PARTS+1,10,20,batch,&visible));
    assert(kzsf_character_draw_native_batch(visibility,5,20,20,batch,&visible) && visible==5);
    for(int i=0;i<5;i++)assert(batch[i]==visibility[i].tagged);
    setup();uintptr_t words[16],table[11];
    for(int i=0;i<16;i++)words[i]=(uintptr_t)i*16;
    words[0]=origin+0x7000;
    for(int i=0;i<11;i++)table[i]=0x1000+(uintptr_t)i;
    memcpy(memory+0x5000,words,sizeof(words));memcpy(memory+0x7000,table,sizeof(table));
    unsigned char before[sizeof(memory)];memcpy(before,memory,sizeof(before));
    KzsfCharacterDrawProxy proxy;uintptr_t methods[11];
    assert(kzsf_character_draw_proxy(origin+0x5000,0x11110000,0x22220000,read_memory,NULL,&proxy,methods));
    assert(!memcmp(before,memory,sizeof(before)));
    for(int i=1;i<16;i++)assert(proxy.fields[i]==words[i]);
    for(int i=0;i<9;i++)assert(methods[i]==table[i]);
    uintptr_t p=(uintptr_t)&proxy;
    assert(((uintptr_t (KZSF_CHARACTER_ABI *)(uintptr_t))methods[9])(p)==0x11110000);
    assert(((uintptr_t (KZSF_CHARACTER_ABI *)(uintptr_t))methods[10])(p)==0x22220000);
    assert(!kzsf_character_draw_proxy(origin+sizeof(memory)-8,1,2,read_memory,NULL,&proxy,methods));
    setup();
    ptr(0x5018,origin+0x4000);integer(0x4058,2);ptr(0x4060,origin+0x8000);
    ptr(0x80c8,origin+0x1110);ptr(0x81b8,origin+0x2220);
    integer(0x5040,2);ptr(0x5048,origin+0x6000);
    integer(0x5058,1);ptr(0x5060,origin+0x7000);
    integer(0x5070,3);ptr(0x5078,origin+0x7800);
    ptr(0x7800,origin+0x6002);ptr(0x7808,origin+0x60b2);ptr(0x7810,origin+0x7002);
    ptr(0x6000,origin+0x8000);ptr(0x60b0,origin+0x80f0);
    ptr(0x6088,origin+0x9000);ptr(0x6138,origin+0x9200);ptr(0x7088,origin+0x9000);
    KzsfCharacterDrawPart parts[KZSF_CHARACTER_MAX_DRAW_PARTS];int32_t count=-1;
    memcpy(before,memory,sizeof(before));
    assert(kzsf_character_draw_parts(origin+0x5000,origin+0x4000,origin+0x1110,origin+0xc000,
        origin,read_memory,NULL,type_of,parts,&count));
    assert(count==3 && parts[0].selected && !parts[1].selected && parts[2].selected && parts[2].combined);
    assert(!memcmp(before,memory,sizeof(before)));
    /* A source-rig combined attachment stays native. Static pieces stay native
     * even when their owned part happens to carry the selected identity. */
    ptr(0x9258,origin+0xc100);ptr(0xc100,origin+0x3c1ec70);ptr(0x7088,origin+0x9200);ptr(0x7800,origin+0x6000);
    assert(kzsf_character_draw_parts(origin+0x5000,origin+0x4000,origin+0x1110,origin+0xc000,
        origin,read_memory,NULL,type_of,parts,&count));
    assert(!parts[0].selected && !parts[1].selected && !parts[2].selected);
    /* Unknown combined meshes use the source pose; bogus backlinks fail before
     * the first original draw, preventing a partial/duplicate draw fallback. */
    ptr(0x7088,origin+0xdead);
    assert(kzsf_character_draw_parts(origin+0x5000,origin+0x4000,origin+0x1110,origin+0xc000,
        origin,read_memory,NULL,type_of,parts,&count) && !parts[2].selected);
    ptr(0x6000,origin+0x8001);
    assert(!kzsf_character_draw_parts(origin+0x5000,origin+0x4000,origin+0x1110,origin+0xc000,
        origin,read_memory,NULL,type_of,parts,&count));
    /* Large native render record lists remain independently bounded from
     * the model's128 owned body/attachment slots. */
    setup();ptr(0x5018,origin+0x4000);integer(0x4058,1);ptr(0x4060,origin+0x8000);
    ptr(0x80c8,origin+0x1110);integer(0x5040,164);ptr(0x5048,origin+0x10000);
    integer(0x5070,164);ptr(0x5078,origin+0x20000);
    for(int i=0;i<164;i++) {
        ptr(0x10000+(size_t)i*0xb0,origin+0x8000);
        ptr(0x10088+(size_t)i*0xb0,origin+0x9000);
        ptr(0x20000+(size_t)i*8,origin+0x10002+(size_t)i*0xb0);
    }
    assert(kzsf_character_draw_parts(origin+0x5000,origin+0x4000,origin+0x1110,origin+0xc000,
        origin,read_memory,NULL,type_of,parts,&count) && count==164 && parts[163].selected);
    integer(0x5070,KZSF_CHARACTER_MAX_DRAW_PARTS+1);
    assert(!kzsf_character_draw_parts(origin+0x5000,origin+0x4000,origin+0x1110,origin+0xc000,
        origin,read_memory,NULL,type_of,parts,&count));
    puts("draw proxy: native-node prefix preserved, source untouched, both SysV pose accessors and bounded reads pass");
    return 0;
}
