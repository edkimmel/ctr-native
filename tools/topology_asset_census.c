#include "platform/native_topology_asset_census.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Hex(const char *s,uint8_t out[32]){for(unsigned i=0;i<32;i++){unsigned x;if(sscanf(s+i*2,"%2x",&x)!=1)return 0;out[i]=(uint8_t)x;}return s[64]=='\0';}
int main(int argc,char **argv){FILE *f;long z;uint8_t *b,hash[32];struct NativeTopologyAssetCensus c;char json[16384];size_t n;if(argc!=3||!Hex(argv[2],hash)){fprintf(stderr,"usage: topology_asset_census <raw-mode2-bin> <sha256>\n");return 2;}f=fopen(argv[1],"rb");if(!f||fseek(f,0,SEEK_END)||((z=ftell(f))<0)||fseek(f,0,SEEK_SET)){if(f)fclose(f);return 2;}b=malloc((size_t)z);if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return 2;}fclose(f);if(!NativeTopologyAssetCensus_FromRawMode2(b,(size_t)z,hash,&c)||!NativeTopologyAssetCensus_ToJson(&c,json,sizeof(json),&n)){free(b);return 1;}free(b);fwrite(json,1,n,stdout);return 0;}
