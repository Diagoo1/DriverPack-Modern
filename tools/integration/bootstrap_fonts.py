"""Fetch missing official Satoshi fonts for a source build; never redistribute them raw."""
from pathlib import Path
import hashlib, io, json, urllib.request, zipfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    folder=ROOT/'assets/fonts'; provenance=json.loads((folder/'PROVENANCE.json').read_text('utf-8'))
    missing=[]
    for name,digest in provenance['files'].items():
        path=folder/name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest()==digest:continue
        if not name.startswith('Satoshi-') or not name.endswith('.ttf'):raise RuntimeError('Missing or modified bundled font asset: '+name)
        missing.append(name)
    if not missing:print('Font fingerprints verified.');return
    request=urllib.request.Request(provenance['english']['source'],headers={'User-Agent':'SDI-Modern-source-build'})
    with urllib.request.urlopen(request,timeout=90) as response:data=response.read(32*1024*1024+1)
    if len(data)>32*1024*1024:raise RuntimeError('Unexpected font archive size.')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        pending={}
        for name in missing:
            member='Satoshi_Complete/Fonts/WEB/fonts/'+name
            if archive.getinfo(member).file_size>4*1024*1024:raise RuntimeError('Unexpected font size.')
            value=archive.read(member)
            if hashlib.sha256(value).hexdigest()!=provenance['files'][name]:raise RuntimeError('Official font fingerprint changed: '+name)
            pending[name]=value
        for name,value in pending.items():(folder/name).write_bytes(value)
    print('Downloaded and verified official static fonts for local embedding.')
if __name__=='__main__':main()
