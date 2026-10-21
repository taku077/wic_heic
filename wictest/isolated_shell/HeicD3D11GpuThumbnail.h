// Research-only reusable Intel D3D11 video processor, hardware P010 -> 256x192 BGRA.
// This is never installed into Windows Explorer. No source HEIC is modified.
// Caller owns a Cache for the duration of the GPU device lifetime; reuse prevents
// re-creating the expensive processor/enumerator/staging texture for every image.
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
inline void Check(bool okay,const char* reason){
 if(!okay)throw std::runtime_error(reason);
}
inline void Hr(HRESULT result,const char* reason){Check(SUCCEEDED(result),reason);}
struct Output {
 std::vector<uint8_t> bgra;
 double setupMs=0,bltMs=0,smallReadbackMs=0,totalMs=0;
 bool reusedProcessor=false;
 UINT sourceWidth=0,sourceHeight=0,sourceSlice=0;
 DXGI_FORMAT sourceFormat=DXGI_FORMAT_UNKNOWN;
};
struct Guard {
 AVD3D11VADeviceContext* context;
 explicit Guard(AVD3D11VADeviceContext* ctx):context(ctx){
  if(context->lock)context->lock(context->lock_ctx);
 }
 ~Guard(){if(context->unlock)context->unlock(context->lock_ctx);}
 Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
};
struct Cache {
 // D3D11 device identity must stay constant while processing a device.
 // Strong ref prevents a dangling device, including in failure paths.
 ComPtr<ID3D11Device> owner;
 ComPtr<ID3D11VideoDevice> videoDevice;
 ComPtr<ID3D11VideoContext> videoContext;
 ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
 ComPtr<ID3D11VideoProcessor> processor;
 ComPtr<ID3D11Texture2D> gpuThumb;
 ComPtr<ID3D11Texture2D> staging;
 ComPtr<ID3D11VideoProcessorOutputView> outputView;
 bool ready=false;
 void Reset(){
  outputView.Reset();staging.Reset();gpuThumb.Reset();
  processor.Reset();enumerator.Reset();videoContext.Reset();
  videoDevice.Reset();owner.Reset();ready=false;
 }
 void Prepare(AVD3D11VADeviceContext* d3d,UINT sourceWidth,UINT sourceHeight){
  Check(d3d&&d3d->device&&d3d->device_context,"FFmpeg D3D11 device/context missing");
  if(ready && owner.Get()==d3d->device)return;
  Reset();
  // Fail closed when an old cache is tied to a different FFmpeg D3D11 device.
  owner=d3d->device;
  Hr(owner->QueryInterface(IID_PPV_ARGS(videoDevice.GetAddressOf())),
     "Hardware video processor device not available");
  Hr(d3d->device_context->QueryInterface(IID_PPV_ARGS(videoContext.GetAddressOf())),
     "Hardware video processor context not available");
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
  desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth=sourceWidth;desc.InputHeight=sourceHeight;
  desc.OutputWidth=kWidth;desc.OutputHeight=kHeight;
  desc.InputFrameRate.Numerator=1;desc.InputFrameRate.Denominator=1;
  desc.OutputFrameRate=desc.InputFrameRate;
  desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  Hr(videoDevice->CreateVideoProcessorEnumerator(&desc,enumerator.GetAddressOf()),
     "Driver cannot enumerate fixed 12MP to 256px video processor");
  UINT inSupported=0,outSupported=0;
  Hr(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_P010,&inSupported),
     "Failed to query video processor P010 format");
  Hr(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM,&outSupported),
     "Failed to query video processor BGRA format");
  Check((inSupported&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)!=0 &&
        (outSupported&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)!=0,
        "Video processor does not support required P010->BGRA conversion");
  D3D11_VIDEO_PROCESSOR_CAPS caps{};
  Hr(enumerator->GetVideoProcessorCaps(&caps),"Video processor capabilities failed");
  Check(caps.RateConversionCapsCount>0&&caps.MaxInputStreams>0,
        "No usable hardware video processor rate/stream");
  Hr(videoDevice->CreateVideoProcessor(enumerator.Get(),0,processor.GetAddressOf()),
     "Creating video processor failed");
  D3D11_TEXTURE2D_DESC tex{};
  tex.Width=kWidth;tex.Height=kHeight;tex.MipLevels=1;tex.ArraySize=1;
  tex.Format=DXGI_FORMAT_B8G8R8A8_UNORM;tex.SampleDesc.Count=1;
  tex.Usage=D3D11_USAGE_DEFAULT;tex.BindFlags=D3D11_BIND_RENDER_TARGET;
  Hr(owner->CreateTexture2D(&tex,nullptr,gpuThumb.GetAddressOf()),
     "Creating reusable small GPU target failed");
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outDesc{};
  outDesc.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
  outDesc.Texture2D.MipSlice=0;
  Hr(videoDevice->CreateVideoProcessorOutputView(gpuThumb.Get(),enumerator.Get(),
    &outDesc,outputView.GetAddressOf()),"Creating reusable video output view failed");
  D3D11_TEXTURE2D_DESC stage=tex;
  stage.Usage=D3D11_USAGE_STAGING;stage.BindFlags=0;
  stage.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Hr(owner->CreateTexture2D(&stage,nullptr,staging.GetAddressOf()),
     "Creating reusable small staging texture failed");
  ready=true;
 }
};
inline Output Render(const AVFrame* frame,AVBufferRef* device,Cache& cache){
 Check(frame&&frame->format==AV_PIX_FMT_D3D11&&frame->hw_frames_ctx&&device,
       "No real FFmpeg D3D11 hardware decoded texture");
 Check(frame->width==4032&&frame->height==3024,
       "GPU thumbnail research limited to fixed 4032x3024 fixture");
 Check(frame->color_range==AVCOL_RANGE_MPEG||
       frame->color_range==AVCOL_RANGE_UNSPECIFIED,
       "Unsupported full range for GPU pilot");
 Check(frame->colorspace==AVCOL_SPC_SMPTE170M||
       frame->colorspace==AVCOL_SPC_BT470BG||
       frame->colorspace==AVCOL_SPC_UNSPECIFIED||
       frame->colorspace==AVCOL_SPC_BT709,
       "Unsupported HDR/SDR color matrix for GPU pilot");
 const auto* hw=reinterpret_cast<const AVHWDeviceContext*>(device->data);
 Check(hw&&hw->type==AV_HWDEVICE_TYPE_D3D11VA&&hw->hwctx,
       "Hardware device context not D3D11VA");
 auto* d3d=reinterpret_cast<AVD3D11VADeviceContext*>(hw->hwctx);
 Check(d3d->device&&d3d->device_context,"GPU driver device/context missing");
 auto* surface=reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
 const auto slice=reinterpret_cast<intptr_t>(frame->data[1]);
 Check(surface&&slice>=0&&slice<256,"Invalid D3D11 decoder array slice");
 Output output;
 output.sourceSlice=static_cast<UINT>(slice);
 output.sourceWidth=UINT(frame->width);
 output.sourceHeight=UINT(frame->height);
 const auto started=Clock::now();
 {
  // Shared FFmpeg device lock prevents concurrent D3D11 context operations.
  Guard lock(d3d);
  D3D11_TEXTURE2D_DESC sourceDesc{};
  surface->GetDesc(&sourceDesc);
  output.sourceFormat=sourceDesc.Format;
  Check(output.sourceSlice<sourceDesc.ArraySize &&
        sourceDesc.Width>=output.sourceWidth &&
        sourceDesc.Height>=output.sourceHeight &&
        sourceDesc.Format==DXGI_FORMAT_P010,
        "GPU source is not a bounded P010 decoded texture");
  output.reusedProcessor=cache.ready&&cache.owner.Get()==d3d->device;
  cache.Prepare(d3d,output.sourceWidth,output.sourceHeight);
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC viewDesc{};
  viewDesc.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
  viewDesc.Texture2D.MipSlice=0;viewDesc.Texture2D.ArraySlice=output.sourceSlice;
  ComPtr<ID3D11VideoProcessorInputView> inputView;
  Hr(cache.videoDevice->CreateVideoProcessorInputView(surface,cache.enumerator.Get(),
     &viewDesc,inputView.GetAddressOf()),
     "Cannot create input view for current decoder array slice");
  // Explicit SDR limited-range 601/709 -> full-range BGRA.
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
  inputColor.YCbCr_Matrix=frame->colorspace==AVCOL_SPC_BT709?1:0;
  inputColor.Nominal_Range=D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE outColor{};
  outColor.RGB_Range=0;
  cache.videoContext->VideoProcessorSetStreamFrameFormat(cache.processor.Get(),0,
      D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  cache.videoContext->VideoProcessorSetStreamColorSpace(cache.processor.Get(),0,&inputColor);
  cache.videoContext->VideoProcessorSetOutputColorSpace(cache.processor.Get(),&outColor);
  RECT source={0,0,LONG(output.sourceWidth),LONG(output.sourceHeight)};
  RECT target={0,0,LONG(kWidth),LONG(kHeight)};
  cache.videoContext->VideoProcessorSetStreamSourceRect(cache.processor.Get(),0,TRUE,&source);
  cache.videoContext->VideoProcessorSetStreamDestRect(cache.processor.Get(),0,TRUE,&target);
  cache.videoContext->VideoProcessorSetOutputTargetRect(cache.processor.Get(),TRUE,&target);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable=TRUE;stream.pInputSurface=inputView.Get();
  const auto ready=Clock::now();
  Hr(cache.videoContext->VideoProcessorBlt(cache.processor.Get(),cache.outputView.Get(),
      0,1,&stream),"Hardware VideoProcessorBlt rejected");
  const auto submitted=Clock::now();
  d3d->device_context->CopyResource(cache.staging.Get(),cache.gpuThumb.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Hr(d3d->device_context->Map(cache.staging.Get(),0,D3D11_MAP_READ,0,&mapped),
     "Small BGRA stage Map failed");
  struct Unmap {
   ID3D11DeviceContext* ctx;ID3D11Texture2D* texture;
   ~Unmap(){ctx->Unmap(texture,0);}
  } unmap{d3d->device_context,cache.staging.Get()};
  Check(mapped.pData&&mapped.RowPitch>=kWidth*4,"Invalid small BGRA staging stride");
  output.bgra.resize(size_t(kWidth)*kHeight*4);
  for(UINT row=0;row<kHeight;++row)
   memcpy(output.bgra.data()+size_t(row)*kWidth*4,
       static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch,
       size_t(kWidth)*4);
  const auto finished=Clock::now();
  auto ms=[](Clock::duration duration){
   return std::chrono::duration<double,std::milli>(duration).count();
  };
  output.setupMs=ms(ready-started);
  output.bltMs=ms(submitted-ready);
  output.smallReadbackMs=ms(finished-submitted);
  output.totalMs=ms(finished-started);
 }
 return output;
}
} // namespace HeicGpuThumb
