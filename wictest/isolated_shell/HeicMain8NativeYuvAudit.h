// Pinned artificial 12MP HEVC Main 8-bit comparison, read-only diagnostic.
// FFmpeg CPU yuv420p and D3D11VA downloaded NV12 are made comparable
// WITHOUT RGB conversion, so decode errors cannot be confused with swscale.
// This is research data only; never writes source, pixels or personal photos.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace HeicNative8 {
using Bytes=std::vector<uint8_t>;
constexpr int W=4032,H=3024;
inline void Require(bool value,const char* message){
 if(!value)throw std::runtime_error(message);
}
inline Bytes Normalize(const AVFrame* f){
 Require(f && f->width==W && f->height==H,
         "Main8 YUV parity is fixed to 4032x3024 synthetic frame");
 Require(f->format==AV_PIX_FMT_YUV420P || f->format==AV_PIX_FMT_NV12,
         "Main8 must be CPU planar YUV420P or GPU NV12");
 Require(f->data[0]&&f->data[1] &&
          (f->format==AV_PIX_FMT_NV12 || f->data[2]),
         "Main8 missing decoded YUV planes");
 const int cw=W/2,ch=H/2;
 Require(f->linesize[0]>=W && f->linesize[1]>=W/2 &&
         (f->format==AV_PIX_FMT_NV12 || f->linesize[2]>=cw),
         "Main8 invalid FFmpeg native YUV stride");
 if(f->format==AV_PIX_FMT_NV12)
  Require(f->linesize[1]>=W,"Main8 invalid interleaved NV12 chroma stride");
 Bytes output(size_t(W)*H*3/2);
 const size_t chromaStart=size_t(W)*H;
 const size_t vStart=chromaStart+size_t(cw)*ch;
 for(int y=0;y<H;++y)
  std::copy_n(f->data[0]+size_t(y)*f->linesize[0],W,
              output.data()+size_t(y)*W);
 for(int y=0;y<ch;++y){
  if(f->format==AV_PIX_FMT_NV12){
   const uint8_t* source=f->data[1]+size_t(y)*f->linesize[1];
   for(int x=0;x<cw;++x){
    output[chromaStart+size_t(y)*cw+x]=source[2*x];
    output[vStart+size_t(y)*cw+x]=source[2*x+1];
   }
  }else{
   std::copy_n(f->data[1]+size_t(y)*f->linesize[1],cw,
               output.data()+chromaStart+size_t(y)*cw);
   std::copy_n(f->data[2]+size_t(y)*f->linesize[2],cw,
               output.data()+vStart+size_t(y)*cw);
  }
 }
 return output;
}
// Zero-copy luma and chroma-only repacking for the GPU NV12 swscale path.
// This keeps original P010/Main10 and CPU yuv420p paths unchanged and needs
// only W*H/2 bytes of temporary memory instead of a full 18MiB YUV copy.
struct PlanarNv12View {
 Bytes chroma;
 const uint8_t* data[4]={nullptr,nullptr,nullptr,nullptr};
 int linesize[4]={0,0,0,0};
};
inline void BuildPlanarNv12View(const AVFrame* frame,PlanarNv12View& output){
 Require(frame&&frame->format==AV_PIX_FMT_NV12 &&
         frame->width==W&&frame->height==H &&
         frame->data[0]&&frame->data[1] &&
         frame->linesize[0]>=W&&frame->linesize[1]>=W,
         "Main8 NV12 planar view rejected unverified frame geometry");
 const int cw=W/2,ch=H/2;
 output.chroma.resize(size_t(W)*H/2);
 uint8_t* const u=output.chroma.data();
 uint8_t* const v=u+size_t(cw)*ch;
 for(int y=0;y<ch;++y){
  const uint8_t* src=frame->data[1]+size_t(y)*frame->linesize[1];
  for(int x=0;x<cw;++x){
   u[size_t(y)*cw+x]=src[2*x];
   v[size_t(y)*cw+x]=src[2*x+1];
  }
 }
 output.data[0]=frame->data[0];
 output.data[1]=u;
 output.data[2]=v;
 output.linesize[0]=frame->linesize[0];
 output.linesize[1]=cw;output.linesize[2]=cw;
}
struct PlaneSummary {
 uint64_t changed=0,absSum=0;
 unsigned maxDiff=0;
 int xmin=-1,ymin=-1,xmax=-1,ymax=-1;
};
inline PlaneSummary ComparePlane(const Bytes& a,const Bytes& b,
                                size_t offset,int w,int h){
 PlaneSummary s;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  const size_t i=offset+size_t(y)*w+x;
  const unsigned diff=unsigned(std::abs(int(a[i])-int(b[i])));
  if(!diff)continue;
  ++s.changed;s.absSum+=diff;s.maxDiff=std::max(s.maxDiff,diff);
  if(s.xmin<0){s.xmin=s.xmax=x;s.ymin=s.ymax=y;}
  else{
   s.xmin=std::min(s.xmin,x);s.xmax=std::max(s.xmax,x);
   s.ymin=std::min(s.ymin,y);s.ymax=std::max(s.ymax,y);
  }
 }
 return s;
}
inline bool Audit(const Bytes& software,const Bytes& hardware,const char* sample){
 Require(software.size()==size_t(W)*H*3/2 &&
         hardware.size()==software.size(),"Missing CPU/GPU full native 8bit data");
 const size_t Ysize=size_t(W)*H,UVsize=Ysize/4;
 const PlaneSummary y=ComparePlane(software,hardware,0,W,H);
 const PlaneSummary u=ComparePlane(software,hardware,Ysize,W/2,H/2);
 const PlaneSummary v=ComparePlane(software,hardware,Ysize+UVsize,W/2,H/2);
 const PlaneSummary* planes[]={&y,&u,&v};
 const char* names[]={"Y","U","V"};
 for(int p=0;p<3;++p){
  const PlaneSummary& s=*planes[p];
  const double pixels=p==0?double(Ysize):double(UVsize);
  std::printf("HEIC_GPU_NATIVE8_PLANE sample=%s plane=%s changed=%llu count=%.0f mae=%.8f max_diff=%u bbox=%d,%d,%d,%d\n",
   sample,names[p],static_cast<unsigned long long>(s.changed),
   pixels,double(s.absSum)/pixels,s.maxDiff,
   s.xmin,s.ymin,s.xmax,s.ymax);
 }
 const bool exact=y.changed==0&&u.changed==0&&v.changed==0;
 std::printf("HEIC_GPU_NATIVE8_COMPARE sample=%s software_layout=yuv420p hardware_layout=nv12 normalized_planar_yuv_exact=%d any_luma_difference=%d any_chroma_difference=%d materialized_pixels_saved_to_disk=0\n",
   sample,int(exact),int(y.changed!=0),int(u.changed!=0||v.changed!=0));
 if(!exact)std::puts("HEIC_GPU_MAIN8_NATIVE_PIXEL_MISMATCH_KEEP_CPU_FALLBACK");
 return exact;
}
} // namespace HeicNative8
