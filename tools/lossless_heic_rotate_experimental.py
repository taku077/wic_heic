#!/usr/bin/env python3
"""Experimental lossless CW90 HEIC rotation: writes a NEW file; never overwrites.
Only pre-existing HEIF primary irot property is supported. No Photos integration.
Unsupported layouts fail closed. Pixel data, ICC, HDR and thumbnails are untouched.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile

class UnsupportedHEIC(Exception): pass
def reject(msg): raise UnsupportedHEIC(msg)
def uint(b,p,n):
    if n<0 or p<0 or p+n>len(b): reject("Truncated HEIC")
    return int.from_bytes(b[p:p+n],"big")
def boxes(b,start,end):
    ans=[]; p=start
    while p<end:
        if p+8>end:reject("Truncated BMFF box")
        size=uint(b,p,4); label=b[p+4:p+8].decode("latin1"); head=8
        if size==1: size=uint(b,p+8,8);head=16
        if size==0:size=end-p
        if size<head or p+size>end:reject("Invalid box size: "+label)
        ans.append((label,p,p+size,p+head));p+=size
    return ans
def one(ary,label):
    matches=[x for x in ary if x[0]==label]
    if len(matches)!=1:reject("Expected one "+label)
    return matches[0]
def props_for_primary(b,ipma,item):
    p=ipma[3];ver=uint(b,p,1);flags=uint(b,p+1,3)
    if ver not in (0,1) or flags&~1:reject("Unsupported ipma version")
    count=uint(b,p+4,4);p+=8
    if count>65535:reject("Excessive ipma entries")
    found=None
    for _ in range(count):
        n=2 if ver==0 else 4;identity=uint(b,p,n);p+=n
        cnt=uint(b,p,1);p+=1;current=[]
        for _ in range(cnt):
            n=2 if flags&1 else 1
            index=uint(b,p,n)&(0x7fff if n==2 else 0x7f);p+=n
            if index:current.append(index)
        if identity==item:
            if found is not None:reject("Duplicate primary association")
            found=current
    if p!=ipma[2] or found is None:reject("Invalid ipma associations")
    return found
def exif_ids(b,iinf):
    p=iinf[3];ver=uint(b,p,1)
    if ver not in (0,1):reject("Unsupported iinf")
    cntlen=2 if ver==0 else 4
    cnt=uint(b,p+4,cntlen);entries=boxes(b,p+4+cntlen,iinf[2])
    if cnt!=len(entries):reject("Invalid iinf count")
    ids=[]
    for entry in entries:
        if entry[0]!='infe':continue
        ver=uint(b,entry[3],1)
        if ver not in (2,3):continue
        n=2 if ver==2 else 4
        identity=uint(b,entry[3]+4,n)
        typ=b[entry[3]+4+n+2:entry[3]+4+n+6]
        if typ==b'Exif':ids.append(identity)
    return ids
def exif_links(b,iref,primary):
    v=uint(b,iref[3],1)
    if v not in (0,1):reject("Unsupported iref")
    n=2 if v==0 else 4
    found=set()
    for entry in boxes(b,iref[3]+4,iref[2]):
        if entry[0]!='cdsc':continue
        p=entry[3];identity=uint(b,p,n);p+=n
        count=uint(b,p,2);p+=2
        if p+n*count!=entry[2]:reject("Invalid cdsc")
        for j in range(count):
            if uint(b,p+j*n,n)==primary:found.add(identity)
    return found
def exif_extent(b,iloc,exifid,idat):
    p=iloc[3];ver=uint(b,p,1)
    if ver not in (0,1,2):reject("Unsupported iloc")
    sz=uint(b,p+4,2);p+=6
    osz=(sz>>12)&15;lsz=(sz>>8)&15;bsz=(sz>>4)&15;isz=sz&15
    if not osz or not lsz or any(i>8 for i in (osz,lsz,bsz,isz)):
        reject("Unsupported iloc field sizes")
    n=2 if ver<2 else 4
    count=uint(b,p,n);p+=n
    if count>65535:reject("Excessive iloc records")
    for _ in range(count):
        n=2 if ver<2 else 4
        ident=uint(b,p,n);p+=n
        method=uint(b,p,2)&15 if ver else 0
        if ver:p+=2
        p+=2 # data_reference_index
        base=uint(b,p,bsz);p+=bsz
        cnt=uint(b,p,2);p+=2
        if cnt>16384:reject("Excessive iloc extents")
        extents=[]
        for _ in range(cnt):
            if ver and isz:p+=isz
            offset=uint(b,p,osz);p+=osz
            length=uint(b,p,lsz);p+=lsz
            extents.append((offset,length))
        if ident!=exifid:continue
        if len(extents)!=1 or extents[0][1]<12:
            reject("Unsupported EXIF extent layout")
        offset,length=extents[0]
        if method==0:absolute=base+offset
        elif method==1 and idat:absolute=idat[3]+base+offset
        else:reject("Unsupported Exif construction method")
        if absolute+length>len(b):reject("Out-of-bounds EXIF extent")
        return absolute,length
    reject("Cannot locate EXIF item")
def orientation(b,start,length):
    tiff=start+4+uint(b,start,4)
    if tiff+8>start+length:reject("Invalid TIFF offset")
    bo=b[tiff:tiff+2]
    if bo==b'II':endian="little"
    elif bo==b'MM':endian="big"
    else:reject("Invalid EXIF byte order")
    def get(p,n):
        if p+n>start+length:reject("Truncated EXIF")
        return int.from_bytes(b[p:p+n],endian)
    if get(tiff+2,2)!=42:reject("Invalid TIFF magic")
    ifd=tiff+get(tiff+4,4);count=get(ifd,2)
    if count>2048:reject("Excessive EXIF fields")
    results=[]
    for j in range(count):
        p=ifd+2+j*12
        if get(p,2)!=274:continue
        if get(p+2,2)!=3 or get(p+4,4)!=1:reject("Bad EXIF orientation field")
        value=get(p+8,2)
        if value not in (1,6,8):reject("Unsupported EXIF orientation")
        results.append((p+8,value,endian))
    if len(results)>1:reject("Multiple EXIF Orientation fields")
    return results[0] if results else None
def transform(original):
    top=boxes(original,0,len(original))
    ft=one(top,'ftyp')
    if b'heic' not in original[ft[3]:ft[2]] and b'heix' not in original[ft[3]:ft[2]]:
        reject("Not a supported HEIC file")
    meta=one(top,'meta');child=boxes(original,meta[3]+4,meta[2])
    pitm=one(child,'pitm');ver=uint(original,pitm[3],1)
    if ver not in (0,1):reject("Unknown pitm")
    primary=uint(original,pitm[3]+4,2 if ver==0 else 4)
    iprp=one(child,'iprp');sub=boxes(original,iprp[3],iprp[2])
    pc=one(sub,'ipco');properties=boxes(original,pc[3],pc[2])
    inds=props_for_primary(original,one(sub,'ipma'),primary)
    if any(i<1 or i>len(properties) for i in inds):reject("Bad property index")
    angles=[properties[i-1] for i in inds if properties[i-1][0]=='irot']
    if len(angles)!=1 or angles[0][2]-angles[0][3]!=1:
        reject("Require one existing primary irot property")
    if any(properties[i-1][0] in ('imir','clap') for i in inds):
        reject("Additional spatial transforms require review")
    pos=angles[0][3];angle=uint(original,pos,1)
    if angle not in (0,1,2,3):reject("Invalid irot angle")
    mods={pos:(angle-1)%4}
    ids=exif_ids(original,one(child,'iinf'))
    assoc=exif_links(original,one(child,'iref'),primary)
    exifs=[i for i in ids if i in assoc]
    if len(exifs)>1:reject("Multiple Exif items attached to primary")
    previous=None
    if exifs:
        idat=next((x for x in child if x[0]=='idat'),None)
        start,length=exif_extent(original,one(child,'iloc'),exifs[0],idat)
        ori=orientation(original,start,length)
        if ori:
            p,value,endian=ori;previous=value
            if value in (6,8):
                if angle%2!=1:reject("Ambiguous pre-existing EXIF orientation")
                if endian=="big":mods[p]=0;mods[p+1]=1
                else:mods[p]=1;mods[p+1]=0
    output=bytearray(original)
    for p,value in mods.items():output[p]=value
    changed=[i for i,(a,b) in enumerate(zip(original,output)) if a!=b]
    if not changed or not set(changed).issubset(mods):reject("Unexpected mutation")
    return bytes(output),{"primary":primary,"old_irot":angle,"new_irot":(angle-1)%4,
        "old_exif_orientation":previous,"modified_byte_offsets":changed,
        "before_sha256":hashlib.sha256(original).hexdigest(),
        "after_sha256":hashlib.sha256(output).hexdigest()}
def write_new(path,data):
    if path.exists():raise FileExistsError("Output already exists")
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,tmp=tempfile.mkstemp(prefix='.heic_lossless_',suffix='.partial',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as handle:
            handle.write(data);handle.flush();os.fsync(handle.fileno())
        os.link(tmp,path) # exclusive destination creation; never replace originals
    finally:os.unlink(tmp)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    args=p.parse_args()
    if args.input.resolve()==args.output.resolve():p.error("Input and output are identical")
    if args.output.exists():p.error("Output exists; overwrite prohibited")
    output,detail=transform(args.input.read_bytes())
    write_new(args.output,output)
    for k,v in detail.items():print(k,"=",v)
    print("LOSSLESS_HEIC_ROTATE_CW90_SUCCESS; SOURCE_UNCHANGED")
if __name__=='__main__':main()
