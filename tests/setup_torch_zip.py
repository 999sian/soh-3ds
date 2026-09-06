#!/usr/bin/env python3
"""Exercise the actual streaming writer and verify ZIP records independently."""
import pathlib, subprocess, tempfile, zipfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='setup-torch-zip-') as temp:
    temp = pathlib.Path(temp)
    exe, archive = temp/'test', temp/'test.zip'
    subprocess.run(['c++', '-std=c++20', '-I'+str(root/'third_party/shipwright/torch/src'),
                    str(root/'tests/setup_torch_zip.cpp'),
                    str(root/'third_party/shipwright/torch/src/archive/StoredZip.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(archive)], check=True)
    assert not pathlib.Path(str(archive)+'.central.tmp').exists()
    with zipfile.ZipFile(archive) as z:
        assert [i.filename for i in z.infolist()] == ['empty', 'duplicate', 'duplicate', 'large']
        assert [z.read(i) for i in z.infolist()] == [b'', b'a\0b', b'c', b'x'*1048576]
        assert all(i.compress_type == zipfile.ZIP_STORED for i in z.infolist())
        assert z.testzip() is None
print('PASS: stored ZIP/CRCs/duplicates; successful output retained; partial output and sidecar removed; unrelated paths retained on failed creation; final flush failure cleaned')
