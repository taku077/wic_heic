// Experimental read-only GPU thumbnail path (12MP Main10 => 256x192 BGRA).
// No CPU transfer of full-resolution YUV. No files, MSI, registry or COM install.
// Requires a genuine D3D11 hardware AVFrame from FFmpeg. Fail closed on any
// driver unsupported state. RGB quality is measured separately versus WIC Fant.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}
namespace HeicGpuThumb {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
constexpr UINT kWidth=256,kHeight=192;
inline void Check(bool okay,const char* message){
 if(!okay)throw std::runtime_error(message);
}
inline void Hr(HRESULT code,const char* reason){Check(SUCCEEDED(code),reason);}
struct Output{
 std::vector<uint8_t> bgra;
 double setupMs=0,bltMs=0,smallReadbackMs=0,totalMs=0;
 UINT sourceWidth=0,sourceHeight=0,sourceSlice=0;
 DXGI_FORMAT sourceFormat=DXGI_FORMAT_UNKNOWN;
};
struct Guard{
 AVD3D11VADeviceContext* ctx;
 explicit Guard(AVD3D11VADeviceContext* p):ctx(p){
  if(ctx->lock)ctx->lock(ctx->lock_ctx);
 }
 ~Guard(){if(ctx->unlock)ctx->unlock(ctx->lock_ctx);}
 Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
};
inline Output Render(const AVFrame* frame,AVBufferRef* device){
 Check(frame&&frame->format==AV_PIX_FMT_D3D11&&device,
       "No actual D3D11 hardware AVFrame");
 Check(frame->hw_frames_ctx && frame->width==4032 && frame->height==3024,
       "GPU thumbnail pilot requires fixed 4032x3024 decode");
 Check(frame->color_range==AVCOL_RANGE_MPEG||
       frame->color_range==AVCOL_RANGE_UNSPECIFIED,
       "GPU thumbnail refuses unverified full-range source");
 Check(frame->colorspace==AVCOL_SPC_SMPTE170M ||
       frame->colorspace==AVCOL_SPC_BT470BG ||
       frame->colorspace==AVCOL_SPC_UNSPECIFIED ||
       frame->colorspace==AVCOL_SPC_BT709,
       "GPU thumbnail refuses unsupported color space/HDR");
 const auto* hwDevice=reinterpret_cast<const AVHWDeviceContext*>(device->data);
 Check(hwDevice && hwDevice->type==AV_HWDEVICE_TYPE_D3D11VA && hwDevice->hwctx,
       "Not a D3D11VA AVHWDeviceContext");
 auto* d3d=reinterpret_cast<AVD3D11VADeviceContext*>(hwDevice->hwctx);
 Check(d3d->device && d3d->device_context,"Missing FFmpeg shared D3D11 device/context");
 auto* decodedTexture=reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
 const intptr_t rawIndex=reinterpret_cast<intptr_t>(frame->data[1]);
 Check(decodedTexture && rawIndex>=0 && rawIndex<256,
       "Invalid decoder array texture index");
 Output out;
 out.sourceSlice=static_cast<UINT>(rawIndex);
 out.sourceWidth=static_cast<UINT>(frame->width);
 out.sourceHeight=static_cast<UINT>(frame->height);
 const auto started=Clock::now();
 {
  // FFmpeg performs the same context serialization during hwframe readback.
  Guard serialize(d3d);
  D3D11_TEXTURE2D_DESC sourceDesc{};
  decodedTexture->GetDesc(&sourceDesc);
  out.sourceFormat=sourceDesc.Format;
  Check(out.sourceSlice<sourceDesc.ArraySize && sourceDesc.Width>=out.sourceWidth &&
        sourceDesc.Height>=out.sourceHeight && sourceDesc.Format==DXGI_FORMAT_P010,
        "Decoder frame is not a supported 12MP P010 GPU texture");
  ComPtr<ID3D11VideoDevice> videoDevice;
  ComPtr<ID3D11VideoContext> videoContext;
  Hr(d3d->device->QueryInterface(IID_PPV_ARGS(videoDevice.GetAddressOf())),
     "D3D11 video processor device interface unavailable");
  Hr(d3d->device_context->QueryInterface(IID_PPV_ARGS(videoContext.GetAddressOf())),
     "D3D11 video processor context interface unavailable");
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
  desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth=out.sourceWidth;desc.InputHeight=out.sourceHeight;
  desc.OutputWidth=kWidth;desc.OutputHeight=kHeight;
  desc.InputFrameRate.Numerator=1;desc.InputFrameRate.Denominator=1;
  desc.OutputFrameRate=desc.InputFrameRate;
  desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  Hr(videoDevice->CreateVideoProcessorEnumerator(&desc,enumerator.GetAddressOf()),
     "Driver cannot enumerate 12MP-to-256px video processor");
  UINT inputSupport=0,outputSupport=0;
  Hr(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_P010,&inputSupport),
     "P010 video processing capability check failed");
  Hr(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM,&outputSupport),
     "BGRA video processing capability check failed");
  Check((inputSupport&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)!=0 &&
        (outputSupport&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)!=0,
        "GPU cannot video-process P010 input to BGRA output");
  D3D11_VIDEO_PROCESSOR_CAPS caps{};
  Hr(enumerator->GetVideoProcessorCaps(&caps),
     "GPU video processor capability read failed");
  Check(caps.RateConversionCapsCount>0 && caps.MaxInputStreams>0,
        "GPU video processor has no usable stream/rate conversion");
  ComPtr<ID3D11VideoProcessor> processor;
  Hr(videoDevice->CreateVideoProcessor(enumerator.Get(),0,processor.GetAddressOf()),
     "GPU cannot create P010-to-BGRA video processor");
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
  inputDesc.FourCC=0;
  inputDesc.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputDesc.Texture2D.MipSlice=0;
  inputDesc.Texture2D.ArraySlice=out.sourceSlice;
  ComPtr<ID3D11VideoProcessorInputView> inputView;
  Hr(videoDevice->CreateVideoProcessorInputView(decodedTexture,enumerator.Get(),
     &inputDesc,inputView.GetAddressOf()),
     "Cannot create video processor view of decoded D3D11 surface");
  D3D11_TEXTURE2D_DESC destDesc{};
  destDesc.Width=kWidth;destDesc.Height=kHeight;destDesc.MipLevels=1;
  destDesc.ArraySize=1;destDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
  destDesc.SampleDesc.Count=1;destDesc.Usage=D3D11_USAGE_DEFAULT;
  destDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> gpuThumb;
  Hr(d3d->device->CreateTexture2D(&destDesc,nullptr,gpuThumb.GetAddressOf()),
     "Cannot allocate small GPU thumbnail render target");
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
  outputDesc.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
  outputDesc.Texture2D.MipSlice=0;
  ComPtr<ID3D11VideoProcessorOutputView> outputView;
  Hr(videoDevice->CreateVideoProcessorOutputView(gpuThumb.Get(),enumerator.Get(),
     &outputDesc,outputView.GetAddressOf()),
     "Cannot create video processor small BGRA target view");
  D3D11_TEXTURE2D_DESC stageDesc=destDesc;
  stageDesc.Usage=D3D11_USAGE_STAGING;stageDesc.BindFlags=0;
  stageDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  Hr(d3d->device->CreateTexture2D(&stageDesc,nullptr,staging.GetAddressOf()),
     "Cannot allocate 256px CPU-readable staging surface");
  // Use only documented SDR 601/709, limited range. Never silently run an
  // HDR or full-range frame through a 601 default and claim good colors.
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
  inputColor.YCbCr_Matrix=(frame->colorspace==AVCOL_SPC_BT709)?1:0;
  inputColor.Nominal_Range=D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
  outputColor.RGB_Range=0; // Full 0-255 RGB for 32bpp BGRA.
  videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(),0,
     D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  videoContext->VideoProcessorSetStreamColorSpace(processor.Get(),0,&inputColor);
  videoContext->VideoProcessorSetOutputColorSpace(processor.Get(),&outputColor);
  RECT sr={0,0,LONG(out.sourceWidth),LONG(out.sourceHeight)};
  RECT dr={0,0,LONG(kWidth),LONG(kHeight)};
  videoContext->VideoProcessorSetStreamSourceRect(processor.Get(),0,TRUE,&sr);
  videoContext->VideoProcessorSetStreamDestRect(processor.Get(),0,TRUE,&dr);
  videoContext->VideoProcessorSetOutputTargetRect(processor.Get(),TRUE,&dr);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable=TRUE;stream.pInputSurface=inputView.Get();
  const auto ready=Clock::now();
  Hr(videoContext->VideoProcessorBlt(processor.Get(),outputView.Get(),0,1,&stream),
     "D3D11 VideoProcessorBlt 12MP-to-256 BGRA rejected");
  const auto submitted=Clock::now();
  d3d->device_context->CopyResource(staging.Get(),gpuThumb.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Hr(d3d->device_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),
     "256px BGRA readback Map failed");
  struct Unmap {
   ID3D11DeviceContext* context;ID3D11Texture2D* texture;
   ~Unmap(){context->Unmap(texture,0);}
  } unmap{d3d->device_context,staging.Get()};
  Check(mapped.pData && mapped.RowPitch>=kWidth*4,
        "Invalid mapped stride in 256px BGRA staging");
  out.bgra.resize(size_t(kWidth)*kHeight*4);
  for(UINT row=0;row<kHeight;++row)
   memcpy(out.bgra.data()+size_t(row)*kWidth*4,
       static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,
       size_t(kWidth)*4);
  const auto finished=Clock::now();
  auto ms=[](Clock::duration d){
    return std::chrono::duration<double,std::milli>(d).count();
  };
  out.setupMs=ms(ready-started);
  out.bltMs=ms(submitted-ready);
  out.smallReadbackMs=ms(finished-submitted);
  out.totalMs=ms(finished-started);
 }
 return out;
}
} // namespace HeicGpuThumb
