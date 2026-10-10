# Make the ModAPI SDK's Args(...) macros work with a standard (non-MSVC-traditional) preprocessor:
# Args(...) yields one parenthesized argument; terminal macros unwrap it with DEPAREN.
import re,sys
PARAMS={'args','argNames','parameters','passedArguments'}
for path in sys.argv[1:]:
    src=open(path,encoding='utf-8-sig').read().split('\n')
    out=[]
    for line in src:
        m=re.match(r'(\s*#define\s+\w+)\(([^)]*)\)(.*)$',line)
        if m and m.group(1).split()[-1]=='Args':
            line='#define Args(...) (__VA_ARGS__)\n#define DEPAREN(X) DP_ESC(DP_ISH X)\n#define DP_ISH(...) DP_ISH __VA_ARGS__\n#define DP_ESC(...) DP_ESC_(__VA_ARGS__)\n#define DP_ESC_(...) DP_VAN ## __VA_ARGS__\n#define DP_VANDP_ISH'
        elif m:
            ps=[p.strip() for p in m.group(2).split(',')]
            body=m.group(3)
            if PARAMS & set(ps) and ('{' in body or '((' in body):
                for p in PARAMS & set(ps):
                    body=re.sub(r'\b%s\b'%p,'DEPAREN(%s)'%p,body)
                line='%s(%s)%s'%(m.group(1),m.group(2),body)
        out.append(line)
    open(path,'w').write('\n'.join(out))
