#include "common.h"
static BOOL CALLBACK exclude(HWND w,LPARAM pid){DWORD owner=0;GetWindowThreadProcessId(w,&owner);if(owner==(DWORD)pid)SetWindowDisplayAffinity(w,WDA_EXCLUDEFROMCAPTURE);return TRUE;}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 try{
 check(co,"Initialize COM");check(MFStartup(MF_VERSION),"Start Media Foundation; Windows N requires Media Feature Pack");
 if(argc<2)throw std::runtime_error("Expected snip, select, record, inspect or selftest");
 std::wstring command=argv[1];int result=0;
 if(command==L"snip"&&argc==4)result=snip(argv[2],argv[3],false);
 else if(command==L"select"&&argc==3)result=snip(L"",argv[2],true);
 else if(command==L"record"&&argc==11)result=record(argv[2],_wtoi(argv[3]),_wtoi(argv[4]),_wtoi(argv[5]),_wtoi(argv[6]),_wtoi(argv[7]),_wtoi(argv[8]),argv[9],std::wstring(argv[10])==L"true");
 else if(command==L"exclude"&&argc==3)EnumWindows(exclude,(LPARAM)_wtoi(argv[2]));
 else if(command==L"selftest"&&argc==3){result=record(argv[2],0,0,640,360,30,1500000,L"none",true,true);if(!result)result=inspect(argv[2]);}
 else if(command==L"inspect"&&argc==3)result=inspect(argv[2]);
 else throw std::runtime_error("Invalid engine arguments");
 MFShutdown();CoUninitialize();return result;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;MFShutdown();if(SUCCEEDED(co))CoUninitialize();return 1;}
}
void savePNG(Bitmap& image,const std::wstring& path){
 GdiFlush();
 ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create PNG factory");
 ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"Create PNG stream");check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open PNG output");
 ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create PNG encoder");check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize PNG encoder");
 ComPtr<IWICBitmapFrameEncode> frame;check(encoder->CreateNewFrame(&frame,nullptr),"Create PNG frame");check(frame->Initialize(nullptr),"Initialize PNG frame");check(frame->SetSize(image.w,image.h),"Set PNG size");
 WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGR;check(frame->SetPixelFormat(&format),"Set PNG format");
 std::vector<BYTE> topDown((size_t)image.w*image.h*4);for(int y=0;y<image.h;y++)memcpy(topDown.data()+(size_t)y*image.w*4,image.pixels+(size_t)(image.h-1-y)*image.w*4,(size_t)image.w*4);
 check(frame->WritePixels(image.h,image.w*4,(UINT)topDown.size(),topDown.data()),"Write PNG pixels");check(frame->Commit(),"Commit PNG frame");check(encoder->Commit(),"Commit PNG");
}
