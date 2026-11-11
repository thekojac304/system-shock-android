/* SPDX-License-Identifier: GPL-3.0-or-later
 * GPU-RENDER1 capability test, NOT a System Shock world renderer.
 * Synthetic indexed texels only; never reads game data or changes app state.
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 64, H = 32 };
static void require(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL %s (EGL=0x%x GL=0x%x)\n", what, eglGetError(), glGetError());
        exit(1);
    }
}
static GLuint shader(GLenum kind, const char *source) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    require(ok, "compile #version 300 es");
    return s;
}
static GLuint texture(int unit, GLint internal, int w, int h, GLenum format, const void *pixels) {
    GLuint t;
    glGenTextures(1, &t);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
    return t;
}
static float attenuation(float x, float y, float lx, float ly) {
    float dx = x-lx, dy = y-ly;
    float a = fmaxf(0.f, 1.f-sqrtf(dx*dx+dy*dy)/1.4f);
    return a*a;
}
static void verify(const unsigned char *pixels, const unsigned char *palette) {
    const float cold[3] = {.12f, .42f, .85f}, warm[3] = {.9f, .35f, .08f};
    int maximum = 0;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        int idx = x/(W/4);
        float px = 2.f*(x+.5f)/W-1.f, py = 2.f*(y+.5f)/H-1.f;
        float a = attenuation(px,py,-.35f,.2f), b = attenuation(px,py,.4f,-.2f);
        for (int c=0; c<4; ++c) {
            int expected;
            if (c==3) expected=255;
            else if (idx==0) expected=(int[]){13,17,19}[c];
            else {
                float light = idx==3 ? 1.f : .18f*.7f + a*cold[c]+b*warm[c];
                expected=(int)lroundf(fminf(255.f,palette[4*idx+c]*light));
            }
            int delta=abs((int)pixels[4*(y*W+x)+c]-expected);
            if (delta>maximum) maximum=delta;
        }
    }
    printf("pixel_reference_max_error=%d (allowed=2)\n",maximum);
    require(maximum<=2,"indexed lookup, RGB overlap/falloff, cutout and emissive pixels");
}
static unsigned long hash(const unsigned char *p, size_t n) {
    unsigned long h=2166136261u;
    for (size_t i=0;i<n;++i) h=((h^p[i])*16777619u)&0xffffffffu;
    return h;
}
int main(void) {
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major,minor,count;
    require(eglInitialize(display,&major,&minor),"initialize EGL default display");
    require(eglBindAPI(EGL_OPENGL_ES_API),"bind GLES");
    const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,0x0040, /* EGL_OPENGL_ES3_BIT_KHR */
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;
    require(eglChooseConfig(display,config_attrs,&config,1,&count)&&count==1,"ES3 pbuffer config");
    const EGLint surface_attrs[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    unsigned long first_hash[2]={0,0};
    for (int generation=0;generation<3;++generation) {
        EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attrs);
        EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
        require(surface!=EGL_NO_SURFACE&&context!=EGL_NO_CONTEXT,"create surface/context");
        require(eglMakeCurrent(display,surface,surface,context),"make current");
        if (!generation) {
            printf("EGL=%d.%d\nGL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\nGLSL=%s\n",
                major,minor,glGetString(GL_VENDOR),glGetString(GL_RENDERER),
                glGetString(GL_VERSION),glGetString(GL_SHADING_LANGUAGE_VERSION));
            GLint size,units;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE,&size);
            glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);
            printf("max_texture_size=%d fragment_texture_units=%d\n",size,units);
        }
        GLuint vs=shader(GL_VERTEX_SHADER,
            "#version 300 es\nlayout(location=0) in vec2 position;\n"
            "out highp vec2 p; void main(){p=position;gl_Position=vec4(p,0.,1.);}\n");
        GLuint fs=shader(GL_FRAGMENT_SHADER,
            "#version 300 es\nprecision highp float;precision highp usampler2D;\n"
            "uniform usampler2D indices;uniform sampler2D palette;in highp vec2 p;out vec4 color;\n"
            "float falloff(vec2 l){float a=max(0.,1.-length(p-l)/1.4);return a*a;}\n"
            "void main(){uint i=texture(indices,p*.5+.5).r;if(i==0u)discard;\n"
            "vec3 base=texelFetch(palette,ivec2(int(i),0),0).rgb;\n"
            "vec3 light=vec3(.18*.7)+falloff(vec2(-.35,.2))*vec3(.12,.42,.85)\n"
            "+falloff(vec2(.4,-.2))*vec3(.9,.35,.08);\n"
            "color=vec4(i==3u?base:base*light,1.);}\n");
        GLuint program=glCreateProgram();
        glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if (!linked) {char log[4096];glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
        require(linked,"link program");glUseProgram(program);
        glUniform1i(glGetUniformLocation(program,"indices"),0);
        glUniform1i(glGetUniformLocation(program,"palette"),1);
        const unsigned char indices[]={0,1,2,3};
        unsigned char palette[256*4]={0};
        const unsigned char colors[]={0,0,0,255,180,145,110,255,70,150,190,255,40,90,230,255};
        memcpy(palette,colors,sizeof(colors));
        GLuint textures[3];
        textures[0]=texture(0,GL_R8UI,4,1,GL_RED_INTEGER,indices);
        textures[1]=texture(1,GL_RGBA8,256,1,GL_RGBA,palette);
        textures[2]=texture(2,GL_RGBA8,W,H,GL_RGBA,NULL);
        GLuint fbo,depth;
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[2],0);
        glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,W,H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"RGBA8 + D24S8 framebuffer");
        const float vertices[]={-1,-1,1,-1,1,1,-1,1};
        const unsigned short elements[]={0,1,2,0,2,3};
        GLuint vao,buffers[2];glGenVertexArrays(1,&vao);glBindVertexArray(vao);
        glGenBuffers(2,buffers);glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(elements),elements,GL_STATIC_DRAW);
        glViewport(0,0,W,H);glDisable(GL_DITHER);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);
        for (int cycle=0;cycle<2;++cycle) {
            if (cycle) {
                palette[4]=90;palette[5]=190;palette[6]=130;
                glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,textures[1]);
                glTexSubImage2D(GL_TEXTURE_2D,0,1,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,palette+4);
            }
            glClearColor(13.f/255,17.f/255,19.f/255,1.f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,NULL);
            unsigned char pixels[W*H*4];
            glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            require(glGetError()==GL_NO_ERROR,"draw and readback");
            verify(pixels,palette);
            unsigned long h=hash(pixels,sizeof(pixels));
            if (!generation) first_hash[cycle]=h;
            else require(first_hash[cycle]==h,"context recreation output stable");
            printf("generation=%d palette_update=%d hash=%08lx\n",generation,cycle,h);
        }
        require(first_hash[0]!=first_hash[1],"palette update changes image without indexed texture upload");
        glDeleteBuffers(2,buffers);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);
        glDeleteRenderbuffers(1,&depth);glDeleteTextures(3,textures);
        glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
        require(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT),"release current");
        require(eglDestroyContext(display,context),"destroy context");
        require(eglDestroySurface(display,surface),"destroy surface");
    }
    eglTerminate(display);
    puts("PASS: GLES3 capability probe only; no Medical geometry, SDL composition or app lifecycle coverage.");
    return 0;
}
