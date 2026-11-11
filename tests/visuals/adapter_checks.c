int main(void) {
    for(int c=0;c<256;++c) {
        ppall[c*3]=ppall[c*3+1]=ppall[c*3+2]=(unsigned char)c;
        for(int row=0;row<16;++row) shading_table[row*256+c]=(unsigned char)(c/(row+1));
        loved_textures[c]=c;
    }
    for(size_t i=0;i<hv_medical_tile_count;++i) {
        const HvTile *t=&hv_medical_tiles[i];
        map[t->y*64+t->x]=(MapElem){t->type,t->floor_height,t->ceiling_height,t->wall,t->floor,t->ceiling};
    }
    horror_visuals_init(); horror_visuals_map_loaded();
    /* Original must short-circuit even if the renderer has not been created. */
    grd_screen=NULL;
    HorrorScope v=horror_visuals_begin_view();
    assert(!horror_visuals_active()); horror_visuals_end_view(v);
    grd_screen=&screen; screen.ltab=shading_table; _fr_clut_list[0]=shading_table;
    _fr_clut_list[1]=other; fillparm=12345; filltype=7;
    horror_visuals_set_mode(1); assert(saves==1 && gShockPrefs.doGamma==28);
    for(int n=0;n<1000;++n) {
        horror_visuals_set_mode(1+n%2);
        v=horror_visuals_begin_view(); assert(horror_visuals_active());
        HorrorScope tile=horror_visuals_bind(30,22,HV_WALL,108,0);
        assert(horror_visuals_scalar_active() && screen.ltab!=shading_table);
        unsigned char *cool=screen.ltab;
        assert(cool==banks[n%2 ? HV_HORROR_COOL : HV_COOL]);
        if(n%2) {
            assert(horror_visuals_light(11,0,0,30,22)==15*256);
            HorrorScope object=horror_visuals_bind(30,22,HV_OBJECT,-1,0);
            assert(horror_visuals_light(11,0,0,30,22)==11*256);
            horror_visuals_restore(object);
            assert(horror_visuals_light(11,0,0,30,22)==15*256);
        }
        HorrorScope exempt=horror_visuals_bind(30,22,HV_OBJECT,-1,1);
        assert(!horror_visuals_scalar_active() && screen.ltab==shading_table);
        horror_visuals_restore(exempt); assert(screen.ltab==cool);
        _fr_curflags=FR_HACKCAM_MASK;
        HorrorScope camera=horror_visuals_begin_view();
        assert(!horror_visuals_active() && screen.ltab==shading_table);
        horror_visuals_end_view(camera); assert(horror_visuals_active() && screen.ltab==cool);
        _fr_curflags=0;
        horror_visuals_restore(tile);
        assert(screen.ltab==shading_table && _fr_clut_list[0]==shading_table && fillparm==12345 && filltype==7);
        HorrorScope outside=horror_visuals_bind(33,23,HV_WALL,219,0);
        assert(!outside.changed && !horror_visuals_scalar_active());
        horror_visuals_restore(outside); horror_visuals_end_view(v);
        assert(!horror_visuals_active());
    }
    unsigned flags[]={FR_PICKUPM_MASK,FR_HACKCAM_MASK,FR_PALETTE_BW,FR_NORENDR_MASK,FR_SOLIDFR_STATIC};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);++i) {
        _fr_curflags=flags[i];v=horror_visuals_begin_view(); assert(!horror_visuals_active());horror_visuals_end_view(v);
    }
    _fr_curflags=0;
    int *bypasses[]={&player_struct.drug_status[DRUG_LSD],&player_struct.drug_status[DRUG_SIGHT],&fullmap.cyber,&gl};
    for(unsigned i=0;i<sizeof(bypasses)/sizeof(bypasses[0]);++i) {
        *bypasses[i]=1;v=horror_visuals_begin_view();assert(!horror_visuals_active());horror_visuals_end_view(v);*bypasses[i]=0;
    }
    UseCutscenePalette=true;v=horror_visuals_begin_view();assert(!horror_visuals_active());horror_visuals_end_view(v);UseCutscenePalette=false;
    curr_clut_table=1;screen.ltab=other;v=horror_visuals_begin_view();assert(!horror_visuals_active());horror_visuals_end_view(v);
    assert(screen.ltab==other);curr_clut_table=0;screen.ltab=shading_table;
    player_struct.level=2;v=horror_visuals_begin_view();assert(!horror_visuals_active());horror_visuals_end_view(v);player_struct.level=1;
    map[20*64+26].wtex=0;horror_visuals_map_loaded();v=horror_visuals_begin_view();assert(!horror_visuals_active());horror_visuals_end_view(v);
    assert(horror_visuals_light(15,15,0,30,22)==0);
    horror_visuals_set_mode(0);assert(!gShockPrefs.doHorrorVisuals && gShockPrefs.doGamma==28);
    puts("VIS1 production adapter / 1000 nested scopes / special-view bypass / Original / invalid-map fallback: PASS");
    return 0;
}
