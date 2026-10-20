import hashlib
from pathlib import Path
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).parent))
from lossless_heic_rotate_experimental import UnsupportedHEIC, transform, write_new

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
print("LOSSLESS_HEIC_OUTPUT_READY_FOR_WIC="+str(fixtureOutput))
print("LOSSLESS_HEIC_FOUR_ROTATION_TEST_PASSED",steps)
print("LOSSLESS_HEIC_SOURCE_UNCHANGED SHA256="+h)
