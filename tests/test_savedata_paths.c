/* Save mounts, metadata and memory must share the selected storage root. */
#undef NDEBUG
#include "../src/runtime_savedata.c"
#include <assert.h>

static char mounted[700];
uintptr_t runtime_lookup(const RuntimeExport *t,size_t n,const char *nid) {
    (void)t; (void)n; (void)nid; return 0;
}
const char *runtime_file_user_dir(void) { return "unused-user-root"; }
int runtime_file_mount(const char *guest,const char *host) {
    (void)guest; snprintf(mounted,sizeof(mounted),"%s",host); return 0;
}
void runtime_file_unmount(const char *guest) { (void)guest; }

int main(int argc,char **argv) {
    assert(argc==2); /* Runner supplies an isolated temporary directory. */
    runtime_savedata_configure("CUSA00190");
    char path[700],expected[700];
    _putenv_s("KZSF_SAVEDATA_DIR","");
    root(1,NULL,path,sizeof(path));
    assert(!strcmp(path,"unused-user-root/savedata/1/CUSA00190"));
    _putenv_s("KZSF_SAVEDATA_DIR",argv[1]);
    root(2,"OTHER",path,sizeof(path));
    snprintf(expected,sizeof(expected),"%s/2/OTHER",argv[1]);
    assert(!strcmp(path,expected));
    initialized=1;
    DirName dir={"KZ4"}; MountResult result;
    assert(mount(1,NULL,&dir,MODE_CREATE,&result)==0);
    snprintf(expected,sizeof(expected),"%s/1/CUSA00190/KZ4",argv[1]);
    assert(!strcmp(mounted,expected));
    Param param;
    assert(runtime_savedata_param(1,"KZ4",&param)==0);
    assert(save_umount(&result.point)==0);
    assert(memory_setup(1,64,NULL)==0);
    char input[]="saved memory",output[sizeof(input)]={0};
    assert(memory_io(input,sizeof(input),0,1)==0);
    assert(memory_io(output,sizeof(output),0,0)==0);
    assert(!memcmp(input,output,sizeof(input)));
    snprintf(expected,sizeof(expected),"%s/1/CUSA00190.memory/memory.dat",argv[1]);
    assert(!strcmp(memory_path,expected));
    puts("Saved-data path tests passed");
    return 0;
}
