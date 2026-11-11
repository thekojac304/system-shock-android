// No commercial data required. Optional argument: an owned, uncompressed
// MOVI file or LG resource archive containing one (e.g. SVGAINTR.RES).
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "lg.h"
#include "2d.h"
grs_canvas* grd_canvas;
#ifndef MOVIE_DECODER_SOURCE
#define MOVIE_DECODER_SOURCE "../../SourceSnapshot/src/Libraries/AFILE/Source/draw4x4.cpp"
#endif
#include MOVIE_DECODER_SOURCE

// Place the LAST valid byte immediately before an inaccessible page. Unlike
// malloc slack/canaries this catches READ overruns, on any system page size.
class Guarded {
    uint8_t* base;
    size_t allocated;
public:
    uint8_t* data;
    size_t size;
    explicit Guarded(size_t n) : size(n) {
#ifdef _WIN32
        SYSTEM_INFO si; GetSystemInfo(&si); size_t page = si.dwPageSize;
#else
        size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
        size_t usable = ((n + page - 1) / page) * page;
        allocated = usable + page;
#ifdef _WIN32
        base = static_cast<uint8_t*>(VirtualAlloc(nullptr, allocated, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!base) throw std::runtime_error("VirtualAlloc");
        DWORD old;
        if (!VirtualProtect(base + usable, page, PAGE_NOACCESS, &old)) throw std::runtime_error("VirtualProtect");
#else
        base = static_cast<uint8_t*>(mmap(nullptr, allocated, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base == MAP_FAILED) throw std::runtime_error("mmap");
        if (mprotect(base + usable, page, PROT_NONE)) throw std::runtime_error("mprotect");
#endif
        data = base + usable - n;
        std::memset(data, 0, n);
    }
    Guarded(const Guarded&) = delete;
    Guarded& operator=(const Guarded&) = delete;
    ~Guarded() {
#ifdef _WIN32
        VirtualFree(base, 0, MEM_RELEASE);
#else
        munmap(base, allocated);
#endif
    }
};

static uint32_t le(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v |= uint32_t(p[i]) << (8*i);
    return v;
}
static void entry(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 3; ++i) p[i] = uint8_t(v >> (8*i));
}
static void huffman_test(bool extended) {
    Guarded huff((extended ? 4112 : 4096)*3), frame(4), dst(4*7);
    // Direct index 4095, or index 0 -> extension 4096 + nibble 15.
    const uint32_t color = 0x3412;
    entry(huff.data + huff.size - 3, (extended ? 4u : 12u)<<20 | color);
    if (extended) entry(huff.data, 4096);
    frame.data[0] = 4;
    frame.data[2] = extended ? 0 : 0xff;
    frame.data[3] = extended ? 0x0f : 0xf0;
    std::memset(dst.data, 0xcc, dst.size);
    grs_canvas canvas{{dst.data, 7}}; grd_canvas = &canvas;
    Draw4x4Reset(nullptr, huff.data);
    Draw4x4(frame.data, 4, 4);
    for (int y=0; y<4; ++y) for (int x=0; x<7; ++x)
        assert(dst.data[y*7+x] == (x<4 ? (x%2 ? 0x34 : 0x12) : 0xcc));
}
static void mask_tests() {
    for (int type=1; type<=4; ++type) for (int transparent=0; transparent<2; ++transparent) {
        Guarded mask(type*2), dst(4*9), colors(16);
        for (int i=0; i<16; ++i) colors.data[i] = uint8_t(40+i);
        if (transparent) colors.data[0] = 0;
        uint64_t packed = 0;
        for (int i=0; i<16; ++i) packed |= uint64_t(i % (1<<type)) << (i*type);
        for (int i=0; i<type*2; ++i) mask.data[i] = uint8_t(packed >> (8*i));
        uint32_t word = uint32_t(type)<<17;
        if (type==1) word |= uint32_t(colors.data[0]) | (uint32_t(colors.data[1])<<8);
        std::memset(dst.data, 0xcc, dst.size);
        Draw4x4Reset(colors.data, nullptr);
        assert(Draw4x4_InternalBeta(&word, 1, dst.data, 9, mask.data) == mask.data+mask.size);
        for (int y=0; y<4; ++y) for (int x=0; x<9; ++x) {
            int index = (y*4+x) % (1<<type);
            int expected = (x>=4 || (transparent && index==0)) ? 0xcc : colors.data[index];
            assert(dst.data[y*9+x] == expected);
        }
    }
}
static void row_test() {
    Guarded huff(4096*3), frame(64), dst(4*640);
    // Repeated direct words span the complete 160-cell scratch array.
    for (int i=0; i<4096; ++i) entry(huff.data+i*3, 0x103412);
    frame.data[0] = 64;
    grs_canvas canvas{{dst.data, 640}}; grd_canvas = &canvas;
    Draw4x4Reset(nullptr, huff.data); Draw4x4(frame.data, 640, 4);
    for (size_t i=0; i<dst.size; ++i) assert(dst.data[i] == (i%2 ? 0x34 : 0x12));
    // Type 6 repeats the preceding control word; type 5 skips the rest.
    entry(huff.data+2048*3, 0x1c0000);
    uint8_t controls[] = {0x40, 0x00, 0x00};
    uint32_t words[2]{}; BitstreamInfo stream(controls, controls+sizeof(controls));
    Draw4x4_InternalAlpha(words, 2, stream);
    assert(words[0] == 0x103412 && words[1] == words[0]);
    entry(huff.data, 0x1a0000);
    uint8_t skips[] = {0x7c,0}; BitstreamInfo skipstream(skips, skips+sizeof(skips));
    entry(huff.data+1984*3, 0x1a0000);
    Draw4x4_InternalAlpha(words, 2, skipstream);
    assert(words[0] == 0xa0001);
}

static void short_code_test() {
    Guarded huff(4096*3), frame(3), dst(16);
    // Six bits encode a short type-5 skip and its five-bit run count. The
    // 12-bit flash lookup must not read the inaccessible byte after the frame.
    for (int i=0; i<4096; ++i) entry(huff.data+i*3, 0x1a0000);
    frame.data[0]=3; frame.data[2]=0x7c;
    std::memset(dst.data,0xcc,dst.size);
    grs_canvas canvas{{dst.data,4}}; grd_canvas=&canvas;
    Draw4x4Reset(nullptr,huff.data); Draw4x4(frame.data,4,4);
    for (size_t i=0;i<dst.size;++i) assert(dst.data[i]==0xcc);
}

static void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
static void movie_test(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot open external movie");
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
    size_t start=0, length=file.size();
    require(length>=128, "Short input");
    if (std::memcmp(file.data(), "MOVI", 4)) {
        require(!std::memcmp(file.data(), "LG Res File", 11), "Expected MOVI or LG resource archive");
        size_t dir=le(file.data()+124,4);
        require(dir<=length-6, "Invalid resource directory");
        size_t n=le(file.data()+dir,2), offset=le(file.data()+dir+2,4);
        require(n<=(length-dir-6)/10, "Short resource directory");
        bool found=false;
        for (size_t i=0; i<n; ++i) {
            const uint8_t* e=file.data()+dir+6+i*10;
            size_t stored=le(e+6,3);
            require(offset<=length && stored<=length-offset, "Invalid resource extent");
            if (stored>=4 && !std::memcmp(file.data()+offset,"MOVI",4)) {
                require(e[5]==0 && le(e+2,3)==stored, "Compressed/compound movie unsupported; supply extracted MOVI");
                start=offset; length=stored; found=true; break;
            }
            offset=(offset+stored+3)&~size_t(3);
        }
        require(found,"No uncompressed MOVI resource found");
    }
    const uint8_t* movie=file.data()+start;
    require(length>=1024,"Short MOVI header");
    int w=le(movie+24,2), h=le(movie+26,2);
    size_t chunks=le(movie+4,4), tablebytes=le(movie+8,4);
    require(w>0 && w<=640 && h>0 && h<=480 && w%4==0 && h%4==0 && le(movie+28,2)==8,"Unsupported movie dimensions/format");
    require(tablebytes<=length-1024 && chunks<=tablebytes/8,"Invalid chunk table");
    Guarded dst(size_t(w)*h);
    grs_canvas canvas{{dst.data,w}}; grd_canvas=&canvas;
    std::unique_ptr<Guarded> colors,huff;
    size_t frames=0; uint64_t hash=14695981039346656037ull; uint32_t lasttime=0;
    for (size_t c=0; c<chunks; ++c) {
        const uint8_t* chunk=movie+1024+c*8;
        uint32_t descriptor=le(chunk,4), type=(descriptor>>24)&7, flags=(descriptor>>27)&15;
        if (type==0) break;
        require(c+1<chunks,"Missing end chunk");
        size_t begin=le(chunk+4,4), end=le(chunk+12,4);
        require(begin<=end && end<=length,"Invalid movie chunk extent");
        size_t size=end-begin; const uint8_t* source=movie+begin;
        if (type==5 && flags==0) {
            colors.reset(new Guarded(size)); std::memcpy(colors->data,source,size);
        } else if (type==5 && flags==1) {
            require(size>=4,"Short Huffman chunk");
            size_t expanded=le(source,4), out=0;
            require(expanded>=4096*3 && expanded<=3*(1u<<20) && expanded%3==0,"Invalid Huffman size");
            huff.reset(new Guarded(expanded));
            for (size_t i=4; out<expanded; i+=4) {
                require(i+4<=size,"Short Huffman runs");
                size_t run=source[i+3]; require(run<= (expanded-out)/3,"Huffman run overflow");
                for (size_t j=0;j<run;++j,out+=3) std::memcpy(huff->data+out,source+i,3);
            }
            Draw4x4Reset(colors ? colors->data : nullptr,huff->data);
        } else if (type==4 && (flags&8)) {
            std::memset(dst.data,0,dst.size);
        } else if (type==1) {
            require(flags==15 && huff && colors && size>=2,"Unsupported video chunk");
            Guarded sourcecopy(size); std::memcpy(sourcecopy.data,source,size);
            require(le(source,2)>=2 && le(source,2)<=size,"Invalid mask offset");
            Draw4x4(sourcecopy.data,w,h);
            // Hash EVERY decoded frame; compare with the old decoder using
            // padded allocations to verify identical pixels throughout.
            for (size_t i=0;i<dst.size;++i) { hash ^= dst.data[i]; hash *= 1099511628211ull; }
            ++frames; lasttime=descriptor&0xffffff;
        }
    }
    require(frames>0,"No video frames");
    std::printf("MOVIE PASS: %dx%d frames=%zu last_time=%.3f pixel_hash=%016llx\n",w,h,frames,lasttime/65536.0,static_cast<unsigned long long>(hash));
}
int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    try {
        if (argc==3 && !std::strcmp(argv[1],"--case")) {
            if (!std::strcmp(argv[2],"root")) huffman_test(false);
            else if (!std::strcmp(argv[2],"extension")) huffman_test(true);
            else if (!std::strcmp(argv[2],"masks")) mask_tests();
            else if (!std::strcmp(argv[2],"short")) short_code_test();
            else throw std::runtime_error("Unknown test case");
            std::puts("CASE PASS"); return 0;
        }
        // --movie-only permits old-decoder comparisons without synthetic tests.
        if (argc==3 && !std::strcmp(argv[1],"--movie-only")) { movie_test(argv[2]); return 0; }
        huffman_test(false); huffman_test(true); mask_tests(); row_test(); short_code_test();
        std::puts("DRAW4X4 PASS: terminal root/extension entries, exact-width masks, transparency, pitch, 640-wide row, repeat/skip, short final code");
        if (argc==2) movie_test(argv[1]);
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
