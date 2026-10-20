#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_character_generic_pose.h"
typedef struct {uintptr_t address;uint32_t size;unsigned char *data;} Block;
static Block blocks[4096];static uint32_t count;
static int read_capture(void *unused,uintptr_t address,void *out,size_t n) {
 (void)unused;
 for(uint32_t i=0;i<count;i++)if(address>=blocks[i].address && address-blocks[i].address<=blocks[i].size && n<=blocks[i].size-(address-blocks[i].address)) {
  memcpy(out,blocks[i].data+address-blocks[i].address,n);return 1;
 }
 return 0;
}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint32_t models;KzsfCharacterMeshLeaf leaves[7]={0};
 assert(fread(&models,4,1,f)==1 && models==7);
 for(unsigned i=0;i<models;i++)assert(fread(&leaves[i].skeleton,8,1,f)==1 && fread(&leaves[i].bindings,8,1,f)==1 && fread(&leaves[i].binding_count,4,1,f)==1);
 assert(fread(&count,4,1,f)==1 && count<4096);
 for(uint32_t i=0;i<count;i++) {
  assert(fread(&blocks[i].address,8,1,f)==1 && fread(&blocks[i].size,4,1,f)==1);
  blocks[i].data=malloc(blocks[i].size);assert(blocks[i].data && fread(blocks[i].data,blocks[i].size,1,f)==1);
 }fclose(f);
 for(unsigned i=1;i<models;i++) {
  KzsfCharacterGenericPlan forward,back;
  int a=kzsf_character_generic_plan(&leaves[0],&leaves[i],read_capture,NULL,&forward);
  int b=kzsf_character_generic_plan(&leaves[i],&leaves[0],read_capture,NULL,&back);
  printf("model%u joints%d forward=%d reverse=%d matched=%d,%d\n",i,leaves[i].binding_count,a,b,a ? forward.matched : 0,b ? back.matched : 0);
  assert(a && b);
  KzsfCharacterMeshLeaf native_layout=leaves[i];
  native_layout.bindings=leaves[0].bindings;native_layout.binding_count=leaves[0].binding_count;
  KzsfCharacterGenericPlan native_to_player;
  assert(kzsf_character_generic_plan(&native_layout,&leaves[0],read_capture,NULL,&native_to_player));
  float native_world[KZSF_CHARACTER_GENERIC_MAX][16],player_world[KZSF_CHARACTER_GENERIC_MAX][16];
  for(int j=0;j<KZSF_CHARACTER_GENERIC_MAX;j++)for(int k=0;k<16;k++)native_world[j][k]=NAN;
  for(int j=0;j<98;j++) {
   int source=native_to_player.source[j];assert(source>=0);
   for(int k=0;k<16;k++)native_world[source][k]=(k%5==0 ? 1.f : 0.f);
   native_world[source][12]=(float)j*.01f;
  }
  assert(kzsf_character_generic_worlds(&native_to_player,native_world,player_world));
  for(int j=0;j<98;j++)for(int k=0;k<16;k++)
   assert(fabsf(player_world[j][k]-native_world[native_to_player.source[j]][k])<1e-4f);
 }
 for(uint32_t i=0;i<count;i++)free(blocks[i].data);return 0;
}
