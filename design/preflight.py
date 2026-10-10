#!/usr/bin/env python3
"""Preflight: overlay every sheet, list unfilled cells and cross-sheet references that don't resolve.
Exit code 1 if anything is open. Build only when this prints CLEAN."""
import json, os, sys
D = os.path.join(os.path.dirname(__file__), 'sheets')
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = {f[:-5]: json.load(open(os.path.join(D, f))) for f in sorted(os.listdir(D)) if f.endswith('.json')}
problems = []
def ids(sheet): return {r['id'] for r in S[sheet]['rows']}
# 1. every row x column crossing is filled
for name, sh in S.items():
    for r in sh['rows']:
        for c in sh['columns']:
            v = r.get(c)
            if v is None or v == '' or v == []:
                if not (c in ('next',) and v == []):
                    problems.append(f'{name}.{r.get("id","?")}.{c}: unfilled')
        extra = set(r) - set(sh['columns'])
        if extra: problems.append(f'{name}.{r["id"]}: cells outside columns {sorted(extra)}')
    seen = set()
    for r in sh['rows']:
        if r['id'] in seen: problems.append(f'{name}.{r["id"]}: duplicate id')
        seen.add(r['id'])
signals = {r['signal'] for r in S['hooks']['rows']}
stages, needs = ids('stages'), ids('needs')
def ref(where, val, pool, what):
    for v in (val if isinstance(val, list) else [val]):
        if v not in pool: problems.append(f'{where}: "{v}" is not a {what}')
for sh in S.values():
    for r in sh['rows']:
        if 'stages' in r: ref(f'{sh["sheet"]}.{r["id"]}.stages', r['stages'], stages, 'stage')
for r in S['restore']['rows']:
    ref(f'restore.{r["id"]}.need', r['need'], needs, 'need')
    ref(f'restore.{r["id"]}.trigger', r['trigger'], signals, 'hook signal')
for r in S['temperature']['rows']:
    ref(f'temperature.{r["id"]}.applies_when', r['applies_when'], signals, 'hook signal')
    ref(f'temperature.{r["id"]}.kind', r['kind'], {'air','wind','bonus'}, 'temperature kind')
for k in ('warmth_drain_per_hour_per_degree_below_zero','warmth_gain_per_hour_per_degree_above_zero','hypothermia_after_hours_at_zero_warmth','hypothermia_condition_multiplier'):
    if k not in S['temperature'].get('rates', {}): problems.append(f'temperature.rates.{k}: unfilled')
wids, cids = ids('weather'), ids('tld_content')
for r in S['weather']['rows']:
    ref(f'weather.{r["id"]}.next', r['next'], wids, 'weather state')
    ref(f'weather.{r["id"]}.sound', r['sound'], cids, 'tld_content row')
    if r['min_hours'] > r['max_hours']: problems.append(f'weather.{r["id"]}: min_hours > max_hours')
for r in S['tld_content']['rows']:
    ref(f'tld_content.{r["id"]}.fallback', r['fallback'], set(S['tld_content']['fallbacks']), 'fallback rule')
    ref(f'tld_content.{r["id"]}.role', r['role'], {'ambience','oneshot','music'}, 'audio role')
hud_binds = needs | {'none', 'feels_like', 'warmth_low', 'weather', 'stage_cell'}
for r in S['hud']['rows']:
    ref(f'hud.{r["id"]}.binds', r['binds'], hud_binds, 'hud binding')
    ref(f'hud.{r["id"]}.kind', r['kind'], {'panel','bar','text','overlay'}, 'hud kind')
    if len(r['color']) != 4: problems.append(f'hud.{r["id"]}.color: needs RGBA')
hook_ids = ids('hooks')
for r in S['stages']['rows']:
    ref(f'stages.{r["id"]}.needs', r['needs'], needs, 'need')
    ref(f'stages.{r["id"]}.spore_check', r['spore_check'], signals, 'hook signal')
    ref(f'stages.{r["id"]}.max_health_source', r['max_health_source'], hook_ids, 'hook')
    for n in r['needs']:
        nr = next(x for x in S['needs']['rows'] if x['id'] == n)
        if r['id'] not in nr['stages']: problems.append(f'stages.{r["id"]}: need {n} does not list this stage')
# every need shown on the HUD in each stage that uses it
for n in S['needs']['rows']:
    bars = [h for h in S['hud']['rows'] if h['binds'] == n['id']]
    if not bars: problems.append(f'needs.{n["id"]}: no hud bar')
    for st in n['stages']:
        if not any(st in b['stages'] for b in bars): problems.append(f'needs.{n["id"]}: no hud bar in stage {st}')
# every signal used somewhere or declared as structural
used = {r['trigger'] for r in S['restore']['rows']} | {r['applies_when'] for r in S['temperature']['rows']} | {r['spore_check'] for r in S['stages']['rows']} | {'always','condition'}
for s in signals - used: problems.append(f'hooks: signal "{s}" is produced but nothing uses it')
for r in S['thresholds']['rows']:
    ref(f'thresholds.{r["id"]}.used_by', r['used_by'], hook_ids, 'hook')
for r in S['tld_content']['rows']:
    if len(r['keywords']) > 11 or len(r['exclude']) > 7: problems.append(f'tld_content.{r["id"]}: too many keywords/excludes for the generated struct')
# every hook signal is implemented in code
src = ''.join(open(os.path.join(R, d, f)).read() for d in ('src/common', 'src/mod') if os.path.isdir(os.path.join(R, d)) for f in os.listdir(os.path.join(R, d)) if f.endswith(('.h', '.cpp')) and not f.endswith('.gen.h'))
for r in S['hooks']['rows']:
    if 'HOOK:' + r['id'] not in src: problems.append(f'hooks.{r["id"]}: no implementation tagged HOOK:{r["id"]} in src/mod')
for r in S['hud']['rows']:
    if 'HUD:' + r['id'] not in src: problems.append(f'hud.{r["id"]}: no implementation tagged HUD:{r["id"]} in src/mod')
for r in S['thresholds']['rows']:
    if 'k_' + r['id'] not in src and r['id'] not in src: problems.append(f'thresholds.{r["id"]}: not used in src')
timing_needed = {'real_seconds_per_hour','hud_refresh_seconds','damage_tick_seconds','howl_min_seconds','howl_max_seconds','music_gap_seconds'}
ref('timing', sorted(timing_needed), ids('timing'), 'timing row')
if problems:
    print(f'PREFLIGHT: {len(problems)} open item(s)'); [print(' -', p) for p in problems]; sys.exit(1)
print(f'PREFLIGHT CLEAN: {sum(len(s["rows"]) for s in S.values())} rows across {len(S)} sheets, all cells filled, all references resolve')
