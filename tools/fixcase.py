# For every #include name used in the given source trees, create a symlink with that exact
# spelling in whichever header dir holds a case-insensitive match (Windows headers on a case-sensitive FS).
import os,re,sys
hdr_roots=sys.argv[1].split(':'); src_roots=sys.argv[2].split(':')
index={}  # lower relpath -> list of (dir, actual relpath)
for r in hdr_roots:
    for d,_,fs in os.walk(r):
        for f in fs:
            p=os.path.relpath(os.path.join(d,f),r)
            index.setdefault(p.lower(),[]).append((r,p))
inc=re.compile(rb'#\s*include\s*[<"]([^>"]+)[>"]')
names=set()
for r in src_roots:
    for d,_,fs in os.walk(r):
        for f in fs:
            try: data=open(os.path.join(d,f),'rb').read()
            except: continue
            for m in inc.findall(data): names.add(m.decode('latin1').replace('\\','/'))
made=0
for n in names:
    for r,actual in index.get(n.lower(),[]):
        tgt=os.path.join(r,n)
        if not os.path.lexists(tgt):
            os.makedirs(os.path.dirname(tgt),exist_ok=True)
            os.symlink(os.path.join(r,actual),tgt); made+=1
print("symlinks",made)
