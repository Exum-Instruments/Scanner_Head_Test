#pragma once

#ifdef CIS_EXPORTS
#define CIS_API __declspec(dllexport)
#else
#define CIS_API __declspec(dllimport)
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>


extern "C" CIS_API void cis_init(int x, int y, int bitcount);
extern "C" CIS_API void create_gamma(unsigned short maxVal);
extern "C" CIS_API int checkFrameData();
extern "C" CIS_API void setScanMode(int md);
extern "C" CIS_API void set2buf(unsigned char buf[], int siz);
extern "C" CIS_API unsigned char* getImage(unsigned char* pixels, int color_type);
extern "C" CIS_API int getRawImage(unsigned char* pixels, short imgX, short imgY, short imgW, short imgH);
extern "C" CIS_API int Raw2Bmp(unsigned char* pBmp, unsigned char* pRaw, short imgW, short imgH, short color_type);

extern "C" CIS_API unsigned char fSync;
extern "C" CIS_API unsigned char syncID1;
extern "C" CIS_API int enableISP;
extern "C" CIS_API int enableGamma;


