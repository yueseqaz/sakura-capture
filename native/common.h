#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr,const char* operation){if(FAILED(hr)){char text[160];sprintf_s(text,"%s (HRESULT 0x%08lX)",operation,(unsigned long)hr);throw std::runtime_error(text);}}
struct Bitmap {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;BYTE* pixels=nullptr;int w=0,h=0;bool topDown=false;
 Bitmap(int width,int height,bool top=false):w(width),h(height),topDown(top){
 dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=topDown?-h:h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,(void**)&pixels,nullptr,0);
 if(!dc||!bitmap)throw std::runtime_error("Cannot allocate screen bitmap");old=SelectObject(dc,bitmap);
 }
 ~Bitmap(){if(dc&&old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
 Bitmap(const Bitmap&)=delete;Bitmap& operator=(const Bitmap&)=delete;
 void capture(int x,int y){HDC screen=GetDC(nullptr);BOOL ok=BitBlt(dc,0,0,w,h,screen,x,y,SRCCOPY|CAPTUREBLT);ReleaseDC(nullptr,screen);GdiFlush();if(!ok)throw std::runtime_error("Screen capture failed; unlock the desktop and try again");}
};
inline RECT normalized(POINT a,POINT b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};}
int snip(const std::wstring& path,const std::wstring& mode,bool selectOnly);
int record(const std::wstring& path,int x,int y,int w,int h,int fps,int bitrate,const std::wstring& audio,bool cursor,bool synthetic=false);
int inspect(const std::wstring& path);
void savePNG(Bitmap& image,const std::wstring& path);
