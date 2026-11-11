/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "horror_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv) {
    assert(argc==3);
    unsigned char base[4096],pal[768],banks[HV_BANK_COUNT][4096],again[HV_BANK_COUNT][4096];
    FILE *f=fopen(argv[1],"rb"); assert(f && fread(base,1,sizeof(base),f)==sizeof(base)); fclose(f);
    f=fopen(argv[2],"rb"); assert(f && fread(pal,1,sizeof(pal),f)==sizeof(pal)); fclose(f);
    for(int row=0;row<16;++row) base[row*256]=255; /* real Init_Lighting policy */
    unsigned char saved[4096]; memcpy(saved,base,sizeof(base));
    hv_build_banks(banks,base,pal); hv_build_banks(again,base,pal);
    assert(!memcmp(banks,again,sizeof(banks)) && !memcmp(base,saved,sizeof(base)));
    assert(!memcmp(banks[HV_ORIGINAL],base,sizeof(base)));
    for(int bank=1;bank<HV_BANK_COUNT;++bank) {
        assert(!memcmp(banks[bank],base,256));
        int changes=0;
        for(int c=0;c<256;++c) {
            int invariant=1;
            for(int row=0;row<16;++row) if(base[row*256+c]!=c) invariant=0;
            for(int row=0;row<16;++row) {
                int old=base[row*256+c],v=banks[bank][row*256+c];
                if(c<32 || c==255 || invariant || old<32 || old==255) assert(v==old);
                if(v!=old) { assert(v>=32 && v<255); ++changes; }
            }
        }
        assert(changes>0); printf("bank=%d remapped_entries=%d/4096\n",bank,changes);
    }
    assert(hv_medical_tile_count==17);
    assert(hv_region_tile(30,22) && hv_region_tile(26,20));
    assert(!hv_region_tile(30,19) && !hv_region_tile(25,22));
    assert(hv_resolve_bank(hv_region_tile(30,22),HV_WALL,108)==HV_COOL);
    assert(hv_resolve_bank(hv_region_tile(26,20),HV_WALL,219)==HV_WARM);
    assert(hv_resolve_bank(hv_region_tile(26,20),HV_CEILING,216)==HV_ORIGINAL);
    for(int c=0;c<2;++c) for(int y=20;y<=25;++y) for(int x=26;x<=31;++x) {
        int previous=-1;
        for(int d=0;d<=15*256;++d) {
            int v=hv_darkness(d,c,x*256,y*256);
            assert(v>=0 && v<=(c ? 12 : 11)*256 && v>=previous);
            previous=v;
        }
    }
    assert(hv_darkness(5*256,0,30*256,24*256)<hv_darkness(5*256,0,28*256,21*256));
    /* Exercise every input sub-row: aggressive response must stay bounded and
     * monotonic, while preserving a real spatial range rather than a dimmer. */
    for(int s=HV_WALL;s<=HV_OBJECT;++s) for(int c=0;c<2;++c)
        for(int y=20;y<=25;++y) for(int x=26;x<=31;++x) {
            int previous=-1;
            for(int d=0;d<=15*256;++d) {
                int v=hv_horror_darkness(d,s,c,x*256,y*256);
                int cap=s==HV_OBJECT ? 11 : s==HV_FLOOR ? 13 : 15;
                assert(v>=0 && v<=cap*256 && v>=previous); previous=v;
            }
        }
    assert(hv_horror_darkness(11*256,HV_WALL,0,30*256,24*256)==2*256);
    assert(hv_horror_darkness(11*256,HV_WALL,0,30*256,22*256)==15*256);
    assert(hv_horror_darkness(0,HV_CEILING,1,30*256,22*256)==10*256);
    assert(memcmp(banks[HV_COOL],banks[HV_HORROR_COOL],4096));
    assert(memcmp(banks[HV_WARM],banks[HV_HORROR_WARM],4096));
    assert(banks[HV_HORROR_COOL][6*256+80]==base[6*256+80]); /* gold control */
    int skin=banks[HV_HORROR_COOL][6*256+128];
    assert(pal[skin*3]>pal[skin*3+1]); /* warm skin stays warm, not cyan */
    if(argc==3) { /* Local QA evidence from actual production bank generator. */
        FILE *dump=fopen("/data/local/tmp/shock-vis1-tests/banks.bin","wb");
        assert(dump && fwrite(banks,1,sizeof(banks),dump)==sizeof(banks)); fclose(dump);
    }
    puts("VIS1 actual palette / protected indices / deterministic banks / region / scalar monotonicity: PASS");
    return 0;
}
