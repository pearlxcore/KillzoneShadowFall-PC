#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_follow_baseline.h"
#include "../src/kzsf_third_person.h"
static int remote(void *opaque,uintptr_t address,void *out,size_t size) {
    SIZE_T got=0;return ReadProcessMemory((HANDLE)opaque,(void*)address,out,size,&got) && got==size;
}
static uintptr_t type(uintptr_t object,uintptr_t base,KzsfZoomRead read,void *opaque) {
    uintptr_t table=0,getter=0;unsigned char code[8];int32_t rel=0;
    if(!object || !read(opaque,object,&table,8) || !table || !read(opaque,table,&getter,8) || getter<base ||
       getter-base>0x1190000-8 || !read(opaque,getter,code,8) || code[0]!=0x48 || code[1]!=0x8d || code[2]!=5 || code[7]!=0xc3)return 0;
    memcpy(&rel,code+3,4);return getter+7+rel;
}

int main(int argc,char **argv) {
 if(argc!=6 && argc!=9)return 2;
 HANDLE process=OpenProcess(PROCESS_VM_READ,FALSE,(DWORD)strtoul(argv[1],0,0));if(!process)return 3;
 uintptr_t view=strtoull(argv[2],0,0),render=strtoull(argv[3],0,0),base=UINT64_C(0x800000000);
 /* Optional explicitly resolved globals for the exact running executable.
  * These are read-only observations, not assumptions about other builds. */
 uintptr_t fraction_address=argc==9 ? strtoull(argv[6],0,0) : 0;
 uintptr_t active_address=argc==9 ? strtoull(argv[7],0,0) : 0;
 uintptr_t hidden_address=argc==9 ? strtoull(argv[8],0,0) : 0;
 FILE *out=fopen(argv[5],"w");if(!out)return 4;
 ULONGLONG start=GetTickCount64();unsigned duration=(unsigned)strtoul(argv[4],0,0);unsigned samples=0;
 while(GetTickCount64()-start<duration*1000u) {
  KzsfFollowBaseline b={0};const char *why="unknown";
  int ok=kzsf_follow_baseline_read(view,render,base,remote,process,type,&b,&why);
  uintptr_t owner=0,state=0,source=0,override=0;int32_t layers=-1;
  remote(process,view+0x18,&owner,8);remote(process,owner+0x900,&state,8);
  remote(process,owner+0x8b8,&override,8);remote(process,view+0x38,&source,8);
  if(source>0x48)remote(process,source-0x48+0x278,&layers,4);
  uintptr_t array=0,layer=0,resource=0,effect=0,effect_table=0,component=0,handle=0,item_inventory=0,item_handle=0;
  int32_t layer_key=-1,direction=-1;unsigned char combat_active=255;
  if(layers==1 && source>0x48) {
   remote(process,source-0x48+0x280,&array,8);remote(process,array,&layer,8);
   remote(process,layer+0x2c,&layer_key,4);remote(process,layer+0x28,&direction,4);
   remote(process,layer+0x70,&resource,8);remote(process,layer+0x50,&effect,8);
   remote(process,effect,&effect_table,8);
  }
  remote(process,owner+0x8f8,&component,8);remote(process,component+0x88,&handle,8);
  remote(process,component+0xdd,&combat_active,1);
  remote(process,owner+0xa48,&item_inventory,8);remote(process,item_inventory+0x68,&item_handle,8);
  KzsfWeaponZoom zoom={0};int zoom_ok=kzsf_weapon_zoom_read(owner,base,remote,process,&zoom);
  float native[16]={0},pose[16];KzsfThirdPerson follow={.requested=1};
  int readable=remote(process,render+0x10,native,64);
  int preview=ok && readable && kzsf_third_person_step(&follow,view,100,1,0,ok,native,pose);
  DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
  float error=0;for(int a=0;a<3;a++)for(int c=a;c<3;c++) {
   float dot=0;for(int i=0;i<3;i++)dot+=native[a*4+i]*native[c*4+i];
   float delta=fabsf(dot-(a==c ? 1.f : 0.f));if(delta>error)error=delta;
  }
  fprintf(out,"{\"ms\":%llu,\"baseline\":%d,\"preview\":%d,\"reason\":\"%s\",\"state_type\":\"%llx\",\"owner\":\"%llx\",\"source\":\"%llx\",\"override\":\"%llx\",\"layers\":%d,\"foreground\":%lu,\"rigid_error\":%.6g,\"matrix\":[",
   (unsigned long long)(GetTickCount64()-start),ok,preview,why,(unsigned long long)type(state,base,remote,process),
   (unsigned long long)owner,(unsigned long long)source,(unsigned long long)override,layers,(unsigned long)foreground,error);
  for(int i=0;i<16;i++)fprintf(out,"%s%.9g",i ? "," : "",native[i]);
  fprintf(out,"],\"layer\":\"%llx\",\"layer_key\":%d,\"layer_direction\":%d,\"layer_resource\":\"%llx\",\"effect_table\":\"%llx\",\"combat_component\":\"%llx\",\"combat_handle\":\"%llx\",\"combat_active\":%u,\"item_type\":\"%llx\",\"zoom_read\":%d,\"zoom_index\":%d",
   (unsigned long long)layer,layer_key,direction,(unsigned long long)resource,
   (unsigned long long)effect_table,
   (unsigned long long)component,(unsigned long long)handle,(unsigned)combat_active,
   (unsigned long long)(item_handle>0x48 ? type(item_handle-0x48,base,remote,process) : 0),zoom_ok,zoom.zoom_index);
  if(argc==9) {
   float fraction=-1;int active=-1,hidden=-1;
   int globals_ok=remote(process,fraction_address,&fraction,4) && remote(process,active_address,&active,4) &&
       remote(process,hidden_address,&hidden,4);
   fprintf(out,",\"globals_read\":%d,\"boom_fraction\":%.6g,\"follow_active\":%d,\"body_hidden\":%d",
       globals_ok,isfinite(fraction) ? fraction : -1,active,hidden);
  }
  fputs("}\n",out);
  fflush(out);samples++;Sleep(20);
 }
 fclose(out);CloseHandle(process);printf("Captured %u read-only camera samples\n",samples);return 0;
}
