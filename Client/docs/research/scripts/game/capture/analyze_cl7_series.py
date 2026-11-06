#!/usr/bin/env python3
"""Análisis nativo pasivo CL7: valida captura completa, sin emulación ni VM."""
import argparse, hashlib, json, math, re, statistics, struct
from collections import Counter
from pathlib import Path
EXPECTED=2250
GLOBAL={'map':(1,8),'scene':(1,8),'worldTime':(1,8),'angles':(3,8),'position':(3,8),'zoom':(1,8),
'distance':(1,8),'targetDistance':(1,8),'farPlane':(1,8),'nearPlane':(1,8),'fieldOfView':(1,8),
'fixedHeight':(1,8),'level':(1,4),'effectTarget':(3,8),'shake':(1,8),'renderSize':(2,8),
'tourState':(1,8),'cullingLevel':(1,8),'mouseUpdate':(1,8),'mouseUpdateMax':(1,8),'pubLayer':(1,8),
'storm':(1,8),'ticksBefore':(1,8),'ticksAfter':(1,8),'heroPointer':(1,8),'mapAfter':(1,8),
'sceneAfter':(1,8),'heroPointerAfter':(1,8),'ticksEntitiesAfter':(1,8)}
HERO={'heroPosition':(3,8),'heroAngles':(3,8),'heroAction':(1,2),'heroSafe':(1,2),
'heroMovement':(1,2),'heroMovementType':(1,2),'heroPath':(33,2)}

def words(s,k,shape=None):
    if k not in s: raise ValueError('missing '+k)
    a=s[k].split(',')
    if shape and (len(a)!=shape[0] or any(not re.fullmatch('[0-9a-f]{%d}'%shape[1],v) for v in a)):
        raise ValueError('bad width/count '+k)
    return [int(v,16) for v in a]

def floats(s,k):
    a=[struct.unpack('<f',struct.pack('<I',v))[0] for v in words(s,k)]
    if not all(math.isfinite(v) for v in a): raise ValueError('nonfinite '+k)
    return a

def check_sample(s):
    for k,shape in GLOBAL.items(): words(s,k,shape)
    for k in ('heroPresent','visualCellPresent','entitiesSampled','boundaryStable'):
        if s.get(k) not in ('0','1'): raise ValueError('bad flag '+k)
    if bool(words(s,'heroPointer')[0])!=(s['heroPresent']=='1'): raise ValueError('pointer/presence disagreement')
    if s['heroPresent']=='1':
        for k,shape in HERO.items(): words(s,k,shape)
    if s['visualCellPresent']=='1':
        for k,shape in {'visualCell':(2,8),'visualCellAttribute':(1,4),'visualCellLayer1':(1,2)}.items(): words(s,k,shape)
        if any(v>255 for v in words(s,'visualCell')): raise ValueError('cell out of bounds')
    if s['entitiesSampled']=='1':
        for k,shape in {'birdAlive':(5,2),'birdType':(5,4),'fishAlive':(3,2),'fishType':(3,4)}.items(): words(s,k,shape)
    stable=all(s[a]==s[b] for a,b in [('map','mapAfter'),('scene','sceneAfter'),('heroPointer','heroPointerAfter')])
    if s['heroPresent']=='1' and s['heroPointer']==s['heroPointerAfter']:
        words(s,'heroPositionAfter',(3,8)); stable=stable and s['heroPosition']==s['heroPositionAfter']
    if stable!=(s['boundaryStable']=='1'): raise ValueError('boundary flag disagreement')

def parse(text,meta):
    if type(meta.get('program_exit')) is not int or meta['program_exit']!=0: raise ValueError('metadata exit not zero')
    if meta.get('numerical_samples')!=EXPECTED or meta.get('numerical_end') is not True or meta.get('numerical_error') is not False:
        raise ValueError('metadata not complete/error-free')
    lines=text.splitlines()
    if not lines or lines[0]!='camera_series 1' or not text.endswith('\n'): raise ValueError('bad header/truncated ending')
    if any(re.search(r'\berror\b',line,re.I) for line in lines): raise ValueError('error marker')
    header={}; samples=[]; cur=None; completed=False; ended=False
    for line in lines[1:]:
        if ended: raise ValueError('trailing content')
        if line.startswith('sample '):
            if cur is not None or completed or line!='sample %d'%len(samples): raise ValueError('bad sample sequence')
            cur={'id':len(samples)}
        elif line=='end_sample':
            if cur is None: raise ValueError('unexpected end_sample')
            check_sample(cur); samples.append(cur); cur=None
        elif line=='end':
            if cur is not None or not completed or len(samples)!=EXPECTED: raise ValueError('premature end')
            ended=True
        elif line.startswith('samplesCompleted='):
            if cur is not None or completed or len(samples)!=EXPECTED or line!='samplesCompleted=000008ca': raise ValueError('bad count/footer')
            completed=True
        elif '=' in line:
            k,v=line.split('=',1); target=cur if cur is not None else header
            if k in target or (cur is None and samples): raise ValueError('duplicate/misplaced field '+k)
            target[k]=v
        elif cur is None and not samples and line.startswith(('field ','heroField ','derived ','entity ')): pass
        else: raise ValueError('unknown schema line')
    if not ended or cur is not None or len(samples)!=EXPECTED: raise ValueError('incomplete capture')
    if header.get('samplesPlanned')!='000008ca' or header.get('rateHz')!='00000019' or header.get('sequence')!='non_atomic': raise ValueError('unexpected plan/rate')
    return header,samples

def dist(a):
    a=sorted(a)
    return {'count':len(a),'min':a[0],'median':statistics.median(a),'mean':statistics.mean(a),'p95':a[math.ceil(len(a)*.95)-1],'max':a[-1]} if a else {'count':0}

def hist(samples,key): return dict(sorted(Counter(s[key] for s in samples if key in s).items()))
def delta(a,b): return (b-a)&0xffffffff

def endpoint(s):
    r={'id':s['id'],'ticks':words(s,'ticksBefore')[0],'map':words(s,'map')[0],'scene':words(s,'scene')[0]}
    if s['heroPresent']=='1': r.update(position=floats(s,'heroPosition'),angles=floats(s,'heroAngles'),action=s['heroAction'],movement=s['heroMovement'],path_state=words(s,'heroPath')[:3])
    if s['visualCellPresent']=='1': r['visual_cell']=words(s,'visualCell')
    return r

def analyze(samples,meta):
    ticks=[words(s,'ticksBefore')[0] for s in samples]
    gaps=[delta(a,b) for a,b in zip(ticks,ticks[1:])]; span=sum(gaps)
    present=[s for s in samples if s['heroPresent']=='1']; pos=[floats(s,'heroPosition') for s in present]
    ent=[s for s in samples if s['entitiesSampled']=='1']
    changes=[s['id'] for a,s in zip(samples,samples[1:]) if a.get('heroPosition')!=s.get('heroPosition')]
    r={'validation':{'complete':True,'program_exit':0,'samples':len(samples),'end_sample_blocks':len(samples),'footer_end':True,'error_markers':False},
       'timing':{'first_to_last_start_ms':span,'observer_rate_hz':(len(samples)-1)*1000/span if span else None,'sample_start_gaps_ms':dist(gaps),
                 'core_read_ms':dist([delta(words(s,'ticksBefore')[0],words(s,'ticksAfter')[0]) for s in samples]),
                 'core_print_entities_ms':dist([delta(words(s,'ticksBefore')[0],words(s,'ticksEntitiesAfter')[0]) for s in samples])},
       'boundary_unstable':{'count':sum(s['boundaryStable']=='0' for s in samples),'ids':[s['id'] for s in samples if s['boundaryStable']=='0']},
       'start':endpoint(samples[0]),'end':endpoint(samples[-1]),'maps':hist(samples,'map'),'scenes':hist(samples,'scene'),
       'unique_raw_camera_states':{k:len({s[k] for s in samples}) for k in ('angles','position','zoom','distance','targetDistance','farPlane','nearPlane','fieldOfView','fixedHeight','level','effectTarget','shake')},
       'hero':{'present_samples':len(present),'position_changes_observed':len(changes),'first_changed_sample':changes[0] if changes else None,
               'last_changed_sample':changes[-1] if changes else None,'unique_positions':len({tuple(p) for p in pos}),
               'unique_visual_cells':len({s['visualCell'] for s in samples if s['visualCellPresent']=='1'}),
               'action_counts':hist(present,'heroAction'),'movement_counts':hist(present,'heroMovement'),
               'movement_type_counts':hist(present,'heroMovementType'),'safe_counts':hist(present,'heroSafe'),
               'path_num_counts':dict(sorted(Counter(str(words(s,'heroPath')[2]) for s in present).items())),
               'path_current_counts':dict(sorted(Counter(str(words(s,'heroPath')[0]) for s in present).items())),
               'path_subpoint_counts':dict(sorted(Counter(str(words(s,'heroPath')[1]) for s in present).items()))},
       'entities':{'observations':len(ent),'sample_ids':[s['id'] for s in ent],
                   'bird_alive_counts':dict(sorted(Counter(str(sum(v!=0 for v in words(s,'birdAlive'))) for s in ent).items())),
                   'fish_alive_counts':dict(sorted(Counter(str(sum(v!=0 for v in words(s,'fishAlive'))) for s in ent).items())),
                   'bird_types':hist(ent,'birdType'),'fish_types':hist(ent,'fishType')},
       'screenshot_frames_in_metadata':len(meta.get('frames',[])),
       'limits':['Lecturas RPM secuenciales, no atomicas, aun con boundaryStable=1.',
                 'GetTickCount es grueso: un intervalo registrado0ms no demuestra duracion nula.',
                 'La cadencia del lector no mide FPS del cliente ni cantidad exacta de pasos logicos.',
                 'La celda visual no es la celda logica protegida; el path puede persistir al quedar quieto.',
                 'core_print_entities_ms incluye impresion y lecturas adicionales de entidades.',
                 'Conteos observados solo describen esos5slots de aves y3de peces en esta serie.',
                 'No se ejecuto emulacion ni se infirio comportamiento no observado.']}
    if pos:
        r['hero']['bounds_xyz']=[[min(p[k] for p in pos),max(p[k] for p in pos)] for k in range(3)]
        r['hero']['net_displacement_xyz']=[pos[-1][k]-pos[0][k] for k in range(3)]
    return r

def negatives(text,meta):
    tests={'truncate_in_block':text[:text.index('end_sample',text.index('sample 1124\n'))],
           'missing_end':text[:text.rfind('end\n')],
           'error_marker_after_footer':text+'error field=ReadProcessMemory\n',
           'incorrect_count':text.replace('samplesCompleted=000008ca','samplesCompleted=000008c9')}
    result={}
    for name,broken in tests.items():
        try: parse(broken,meta)
        except ValueError as e: result[name]={'rejected':True,'reason':str(e)}
        else: raise AssertionError('negative test accepted '+name)
    bad=dict(meta,program_exit=1)
    try: parse(text,bad)
    except ValueError as e: result['metadata_exit_nonzero']={'rejected':True,'reason':str(e)}
    else: raise AssertionError('nonzero exit accepted')
    return result

def report(r):
    t=r['timing']; h=r['hero']; e=r['entities']
    moving = h['position_changes_observed'] > 0
    description = ('Se observo desplazamiento del heroe. Las muestras no equivalen a pasos o frames consecutivos.'
                   if moving else
                   'No se observo desplazamiento del heroe en esta serie. Estos datos no acreditan una vuelta caminando.')
    return '\n'.join([
        '# Serie CL7: captura completa, '+('con movimiento observado' if moving else 'sin traslado observado del heroe'),'',
        description,'',
        'Captura completa:2250 bloques cerrados, footer end, sin error y metadata program_exit=0.',
        f"Intervalo entre primera y ultima lectura:{t['first_to_last_start_ms']}ms; cadencia observada del lector:{t['observer_rate_hz']:.6f}Hz. No son FPS del cliente.",
        f"Intervalo RPM core(ms):{t['core_read_ms']}. Muestras con frontera cambiante:{r['boundary_unstable']['count']}.",'',
        'Inicio: `'+json.dumps(r['start'],ensure_ascii=False)+'`.',
        'Final: `'+json.dumps(r['end'],ensure_ascii=False)+'`.',
        f"Posiciones distintas:{h['unique_positions']}; cambios observados:{h['position_changes_observed']}; celdas visuales distintas:{h['unique_visual_cells']}.",
        'Acciones(hex→muestras): `'+json.dumps(h['action_counts'])+'`.',
        'Movement(hex→muestras): `'+json.dumps(h['movement_counts'])+'`; PathNum(decimal→muestras): `'+json.dumps(h['path_num_counts'])+'`.',
        f"Observaciones de entidades:{e['observations']}; aves vivas(cantidad→observaciones):{e['bird_alive_counts']}; peces:{e['fish_alive_counts']}.",
        f"Frames screenshot declarados por metadata:{r['screenshot_frames_in_metadata']}; este analisis no los interpreta.",'',
        'Pruebas negativas: truncacion dentro de bloque, falta de end, error despues del footer, count incorrecto y exit no cero rechazados.','',
        *['- '+limit for limit in r['limits']],'' ])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('series',type=Path)
    p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--json',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    args=p.parse_args()
    if args.series.name=='progress.txt': p.error('progress.txt is never a final capture')
    raw=args.series.read_bytes(); text=raw.decode('utf-8'); meta=json.loads(args.metadata.read_text())
    try:
        header,samples=parse(text,meta)
        result=analyze(samples,meta)
        # Negative variants operate on normalized text, preserving final-capture validation above.
        result['negative_tests']=negatives('\n'.join(text.splitlines())+'\n',meta)
    except (ValueError,KeyError) as e: p.error(str(e))
    result['source']={'file':str(args.series),'sha256':hashlib.sha256(raw).hexdigest(),
                      'metadata':str(args.metadata),'metadata_sha256':hashlib.sha256(args.metadata.read_bytes()).hexdigest()}
    args.json.write_text(json.dumps(result,indent=2,ensure_ascii=False,allow_nan=False)+'\n')
    args.report.write_text(report(result))
    print(json.dumps({'valid':True,'samples':len(samples),'observer_rate_hz':result['timing']['observer_rate_hz'],
                      'position_changes':result['hero']['position_changes_observed'],'unique_visual_cells':result['hero']['unique_visual_cells'],
                      'boundary_unstable':result['boundary_unstable']['count'],'json':str(args.json)},ensure_ascii=False))

if __name__=='__main__': main()
