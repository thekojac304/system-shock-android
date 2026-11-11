static void check(const char *text, int expected) {
    FILE *f=fopen(GetPrefsPathFilename(),"wb"); assert(f);
    fputs(text,f); fclose(f);
    gShockPrefs.doHorrorVisuals=true;
    gShockPrefs.doGamma=28;
    assert(LoadPrefs()==0 && gShockPrefs.doHorrorVisuals==expected);
    assert(gShockPrefs.doGamma==28);
}
int main(void) {
    check("",0);
    check("gamma = 28\nsoundtrack = remake\n",0);
    check("world-visuals = original\n",0);
    check("world-visuals = enhanced\n",1);
    check("world-visuals = EnHaNcEd\n",1);
    check("world-visuals = conservative\n",1);
    check("world-visuals = HoRrOr\n",2);
    assert(gShockPrefs.postFxMode==3);
    check("world-post-fx = 0\n",0);
    assert(gShockPrefs.postFxMode==0);
    check("world-post-fx = 2\n",0);
    assert(gShockPrefs.postFxMode==2);
    assert(SavePrefs()==0);
    gShockPrefs.postFxMode=0;
    assert(LoadPrefs()==0 && gShockPrefs.postFxMode==2);
    check("world-post-fx = 99\n",0);
    assert(gShockPrefs.postFxMode==3);
    check("world-visuals = HoRrOr\n",2);
    assert(SavePrefs()==0);
    gShockPrefs.doHorrorVisuals=0;
    assert(LoadPrefs()==0 && gShockPrefs.doHorrorVisuals==2 && gShockPrefs.doGamma==28);
    check("world-visuals = invalid\n",0);
    check("world-visuals = \n",0);
    check("world-visuals = enhanced\ngamma = 28\nsoundtrack = remake\n",1);
    assert(SavePrefs()==0);
    FILE *f=fopen(GetPrefsPathFilename(),"rb"); assert(f);
    char data[4096]={0}; assert(fread(data,1,sizeof(data)-1,f)>0); fclose(f);
    assert(strstr(data,"world-visuals = enhanced") && strstr(data,"gamma = 28"));
    assert(strstr(data,"soundtrack = remake"));
    gShockPrefs.doHorrorVisuals=false;
    assert(LoadPrefs()==0 && gShockPrefs.doHorrorVisuals && gShockPrefs.doGamma==28);
    gShockPrefs.doHorrorVisuals=false; assert(SavePrefs()==0);
    gShockPrefs.doHorrorVisuals=true;
    assert(LoadPrefs()==0 && !gShockPrefs.doHorrorVisuals);
    puts("VIS1 production preferences / old files / invalid values / round-trip / gamma: PASS");
    return 0;
}
