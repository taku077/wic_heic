import hashlib
from pathlib import Path
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).parent))
from lossless_heic_rotate_experimental import (UnsupportedHEIC, transform, write_new,
    boxes, one, uint, exif_ids, exif_links, exif_extent, orientation,
    props_for_primary)

fixture=Path("wictest/samples/fixture-irot3-exif6-noaux.heic")
before=fixture.read_bytes()
h=hashlib.sha256(before).hexdigest()
steps=[]
data=before
for i in range(4):
    newer,result=transform(data)
    assert len(newer)==len(before)
    assert len(result["modified_byte_offsets"]) in (1,2)
    assert newer!=data
    steps.append(result)
    data=newer
assert [x["old_irot"] for x in steps]==[3,2,1,0],steps
assert [x["new_irot"] for x in steps]==[2,1,0,3],steps
assert steps[0]["old_exif_orientation"]==6,steps
assert steps[1]["old_exif_orientation"]==1,steps
assert hashlib.sha256(before).hexdigest()==h
# IFD Orientation is normalized to one, so a 4-turn cycle does not
# restore all metadata bytes of a source with redundant EXIF Orientation=6.
assert sum(a!=b for a,b in zip(data,before)) in (1,2)
for bad in (b"bad",before[:100]):
    try:
        transform(bad)
    except UnsupportedHEIC:
        pass
    else:
        raise AssertionError("Malformed HEIC accepted")
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)/"output.heic"
    rotated,_=transform(before)
    write_new(out,rotated)
    assert out.read_bytes()==rotated
    try:
        write_new(out,rotated)
    except FileExistsError:
        pass
    else:
        raise AssertionError("Output overwrite was accepted")
# A temporary synthetic output is consumed by the native Windows WIC
# regression harness in the same CI job. This file is never committed.
fixtureOutput=Path("wictest/samples/fixture-lossless-cw90.heic")
assert not fixtureOutput.exists(), "Never overwrite existing fixture"
rotated,_=transform(before)
write_new(fixtureOutput,rotated)
rotation180,_=transform(rotated)
rotation270,_=transform(rotation180)
for label,contents in (("cw180",rotation180),("cw270",rotation270)):
    new_path=Path("wictest/samples/fixture-lossless-"+label+".heic")
    assert not new_path.exists(), "Never overwrite existing fixture"
    write_new(new_path,contents)
    print("LOSSLESS_HEIC_OUTPUT_READY_FOR_WIC="+str(new_path))
print("LOSSLESS_HEIC_OUTPUT_READY_FOR_WIC="+str(fixtureOutput))
# Construct a PUBLIC SYNTHETIC irot2 + Exif Orientation3 test fixture
# from the existing artificially generated test HEIC. Do not use user photos.
half_turn=bytearray(rotated)     # existing CW90 result: irot2, EXIF1
top=boxes(half_turn,0,len(half_turn))
meta=one(top,"meta")
children=boxes(half_turn,meta[3]+4,meta[2])
pitm=one(children,"pitm")
primary=uint(half_turn,pitm[3]+4,2 if uint(half_turn,pitm[3],1)==0 else 4)
exifs=exif_ids(half_turn,one(children,"iinf"))
linked=exif_links(half_turn,one(children,"iref"),primary)
target_exifs=[x for x in exifs if x in linked]
assert len(target_exifs)==1, "Synthetic half-turn requires exactly one primary EXIF"
idat=next((x for x in children if x[0]=="idat"),None)
start,length=exif_extent(half_turn,one(children,"iloc"),target_exifs[0],idat)
tag=orientation(half_turn,start,length)
assert tag is not None and tag[1]==1
pos,_,endian=tag
half_turn[pos:pos+2]=(3).to_bytes(2,endian)
half_turn_bytes=bytes(half_turn)
# Deliberately no encoded-pixel changes: one EXIF byte only.
assert len(half_turn_bytes)==len(rotated)
assert sum(x!=y for x,y in zip(half_turn_bytes,rotated))==1
original_half_turn=Path("wictest/samples/fixture-irot2-exif3-noaux.heic")
assert not original_half_turn.exists(), "Never overwrite synthetic half-turn fixture"
write_new(original_half_turn,half_turn_bytes)
print("SYNTHETIC_IROT2_EXIF3_SOURCE_READY_FOR_WIC="+str(original_half_turn))
# Negative control: identical EXIF3 metadata, but HEIF explicitly declares
# irot=0. WIC must NOT normalize orientation because no half-turn was applied.
no_half=bytearray(half_turn_bytes)
iprp=one(children,"iprp")
sub=boxes(no_half,iprp[3],iprp[2])
props=boxes(no_half,one(sub,"ipco")[3],one(sub,"ipco")[2])
associations=props_for_primary(no_half,one(sub,"ipma"),primary)
rotations=[props[i-1] for i in associations if props[i-1][0]=="irot"]
assert len(rotations)==1 and uint(no_half,rotations[0][3],1)==2
no_half[rotations[0][3]]=0
assert sum(x!=y for x,y in zip(no_half,half_turn_bytes))==1
no_half_path=Path("wictest/samples/fixture-irot0-exif3-noaux.heic")
assert not no_half_path.exists(), "Never overwrite negative control"
write_new(no_half_path,bytes(no_half))
print("SYNTHETIC_IROT0_EXIF3_NEGATIVE_CONTROL_READY_FOR_WIC="+str(no_half_path))

print("LOSSLESS_HEIC_FOUR_ROTATION_TEST_PASSED",steps)
print("LOSSLESS_HEIC_SOURCE_UNCHANGED SHA256="+h)
