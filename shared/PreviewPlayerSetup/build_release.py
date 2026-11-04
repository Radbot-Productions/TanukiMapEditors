"""Package the tested cooked player; outputs belong to an automation scope."""
import argparse, hashlib, json, pathlib, zipfile

parser=argparse.ArgumentParser()
parser.add_argument('--player',required=True)
parser.add_argument('--output',required=True)
args=parser.parse_args()
root=pathlib.Path(args.player).resolve(); output=pathlib.Path(args.output).resolve()
output.mkdir(parents=True,exist_ok=False)
descriptor=json.loads((root/'preview.json').read_text(encoding='utf-8-sig'))
assert descriptor['protocol']==1 and descriptor['maxSchemaVersion']==5
files=[]
for path in sorted(root.rglob('*')):
    if not path.is_file(): continue
    rel=path.relative_to(root)
    if path.is_symlink(): raise ValueError('Refusing linked player files')
    if rel.parts[0] not in ('Engine','Tanuki') and str(rel) not in ('TanukiMapPreview.exe','preview.json','NOTICES.txt'): continue
    if any(part in ('Saved','Intermediate','Source','DerivedDataCache') for part in rel.parts): continue
    if path.suffix.lower() in ('.pdb','.target','.log','.dmp','.utrace','.ucache'): continue
    files.append({'path':rel.as_posix(),'bytes':path.stat().st_size})
assert any(f['path']=='Tanuki/Binaries/Win64/TanukiMapPreview.exe' for f in files)
archive=output.parent/'player.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_STORED,allowZip64=True) as z:
    for file in files: z.write(root/file['path'],file['path'])
parts=[]; whole=hashlib.sha256(); count=0
with archive.open('rb') as source:
    while count<archive.stat().st_size:
        name=f'Tanuki-Preview-Windows-0.2.0-preview.3.zip.part-{len(parts)+1:03}'
        digest=hashlib.sha256(); size=0
        with (output/name).open('xb') as target:
            while size<1536*1024*1024:
                data=source.read(min(1024*1024,1536*1024*1024-size))
                if not data: break
                target.write(data);digest.update(data);whole.update(data);size+=len(data)
        count+=size
        parts.append(dict(name=name,url='https://github.com/Radbot-Productions/TanukiMapEditors/releases/download/player-v0.2.0-preview.3/'+name,sha256=digest.hexdigest(),bytes=size))
manifest=dict(protocol=1,version='0.2.0-preview.3',maxSchemaVersion=5,archiveSha256=whole.hexdigest(),archiveBytes=count,uncompressedBytes=sum(f['bytes'] for f in files),entryPoint='TanukiMapPreview.exe',parts=parts,files=files)
(output/'preview-player.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(files=len(files),bytes=count,parts=len(parts),sha256=whole.hexdigest())))
