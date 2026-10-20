"""Read-only Chapter3 bundle-reference scan. Strings are references, not placement proof."""
import importlib.util,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'launcher'))
import kzsf_local
game=Path(kzsf_local.load_preferences()['game_dir']);lumps=game/'localcachepink/lumps'
spec=importlib.util.spec_from_file_location('decoder',ROOT/'tools/inventory-kzsf-shaders.py');d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
lz=d.Lz4(Path('C:/msys64/clang64/bin/liblz4.dll'))
files=sorted(lumps.glob('levels.single_player.the_cassandra*.core*'))+sorted(lumps.glob('assets_description.levellist_levellist_assets.core*'))
report={'read_only':True,'scope':'all Cassandra-prefixed base and patch decoded strings; not applied patch graph or placement proof','files':[]}
for n,p in enumerate(files):
 refs=set();names=set();carry=b'';total=0
 for off,b in d.blocks(p,lz):
  total+=len(b);data=carry+b
  refs.update(m.group().decode() for m in re.finditer(rb'assets_description\.characters\.[a-zA-Z0-9_]+',data))
  if 'leveldescription' in p.name or 'levellist' in p.name:
   names.update(m.group().decode(errors='replace') for m in re.finditer(rb'[ -~]{8,}',data) if any(x in m.group().lower() for x in [b'cassandra',b'doctor',b'chapter',b'section_',b'chapter3']))
  carry=data[-512:]
 report['files'].append({'file':p.name,'bytes':p.stat().st_size,'decoded_bytes':total,'character_refs':sorted(refs),'level_strings':sorted(names)})
 print(n+1,len(files),p.name,len(refs),flush=True)
report['character_refs']=sorted({r for f in report['files'] if '.the_cassandra.' in f['file'] for r in f['character_refs']})
(ROOT/'out/chapter3-bundle-membership.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report['character_refs'],indent=2))
