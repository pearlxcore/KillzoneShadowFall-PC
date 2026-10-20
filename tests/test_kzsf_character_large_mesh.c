#define main registry_fixture_checks
#include "test_kzsf_character_resources.c"
#undef main

enum { ROOT=0x10000, GROUPS=0x10200, ARRAYS=0x10800, LEAVES=0x18000 };

static void composite(size_t mesh,uintptr_t rtti,int count,size_t array) {
    ptr(mesh,origin+rtti);integer(mesh+0x58,count);ptr(mesh+0x60,origin+array);
}

/* LOD -> Multi/Switch -> distinct skinned leaves. Every native array stays
 * within its own128-child bound, so256/257 test the total-leaf limit rather
 * than accidentally failing at an unrelated per-composite child bound. */
static void setup_large(int count) {
    setup();
    int groups=(count+127)/128;
    composite(ROOT,0x3b50078,groups,ARRAYS);
    for(int group=0;group<groups;group++) {
        size_t node=GROUPS+(size_t)group*0x100;
        size_t array=0x11000+(size_t)group*0x2800;
        int first=group*128,n=count-first;if(n>128)n=128;
        uintptr_t rtti=group%2 ? 0x3b50778 : 0x3b4feb8;
        size_t stride=group%2 ? 0x10 : 0x50;
        composite(node,rtti,n,array);ptr(ARRAYS+(size_t)group*0x10,origin+node);
        for(int i=0;i<n;i++) {
            size_t mesh=LEAVES+(size_t)(first+i)*0x90;
            leaf(mesh,0xb000);ptr(array+(size_t)i*stride,origin+mesh);
        }
    }
}

int main(void) {
    KzsfCharacterMeshTree tree,before;
    unsigned char saved[sizeof(memory)];
    const int accepted[]={164,256};
    for(unsigned test=0;test<sizeof(accepted)/sizeof(*accepted);test++) {
        int count=accepted[test];setup_large(count);memcpy(saved,memory,sizeof(saved));
        assert(kzsf_character_mesh_read(origin+ROOT,origin,read_memory,NULL,type_of,&tree));
        assert(tree.count==count && !tree.unknown_count && tree.root==origin+ROOT);
        for(int i=0;i<count;i++) {
            assert(tree.leaves[i].mesh==origin+LEAVES+(uintptr_t)i*0x90);
            assert(tree.leaves[i].skeleton==origin+0xc000 && tree.leaves[i].binding_count==32);
        }
        assert(!memcmp(saved,memory,sizeof(saved)));
    }
    setup_large(257);memset(&tree,0xa5,sizeof(tree));before=tree;
    KzsfCharacterMeshWalk walk;memset(&walk,0,sizeof(walk));
    assert(!kzsf_character_mesh_walk(&walk,origin+ROOT,origin,read_memory,NULL,type_of,0));
    assert(walk.result.count==256 && walk.visits==261); // exact total-leaf refusal
    assert(!kzsf_character_mesh_read(origin+ROOT,origin,read_memory,NULL,type_of,&tree));
    assert(!memcmp(&tree,&before,sizeof(tree)));
    setup_large(164);ptr(0x11000,origin+ROOT); // Multi child cycles to LOD root
    assert(!kzsf_character_mesh_read(origin+ROOT,origin,read_memory,NULL,type_of,&tree));
    assert(!memcmp(&tree,&before,sizeof(tree)));
    puts("large mesh:164/256 distinct LOD-Multi-Switch leaves,257 refusal, cycles and read-only/output preservation pass");
    return 0;
}
