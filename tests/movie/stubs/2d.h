#pragma once
// Only the two canvas fields consumed by the production 4x4 decoder.
struct TestBitmap { unsigned char* bits; int row; };
struct grs_canvas { TestBitmap bm; };
extern grs_canvas* grd_canvas;
