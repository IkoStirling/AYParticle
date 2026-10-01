"""Rebuild deterministic, original particle sprites and schema-2 library assets.
Python standard library only. Run from any directory; no external image assets.
"""
from pathlib import Path
import copy, json, math, struct, zlib

MODULE = Path(__file__).resolve().parents[1]
ROOT = MODULE / 'examples' / 'ParticleLibrary'
ASSETS = ROOT / 'Assets'

def png(path, width, height, pixel):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            alpha = max(0, min(1, pixel(x, y)))
            rows.extend((255, 255, 255, round(alpha * 255)))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))

def smoke(x, y):
    u, v = (x-31.5)/31.5, (y-31.5)/31.5
    r = math.hypot(u,v)
    scallop = .10*math.sin(math.atan2(v,u)*7) + .06*math.sin(u*13+v*9)
    return max(0, 1-r/(.92+scallop))**1.5 * .85

def flame(x, y):
    u, v = (x-31.5)/31.5, y/63
    width = .08 + .55*v
    return math.exp(-3*(u/width)**2) * math.sin(math.pi*v)**.65

def explosion(x, y):
    frame = x//64 + 4*(y//64)
    u, v = ((x%64)-31.5)/31.5, ((y%64)-31.5)/31.5
    radius = .28 + frame*.085
    edge = radius*(1+.08*math.sin(math.atan2(v,u)*9+frame*.8))
    return max(0, 1-math.hypot(u,v)/edge)**.45

png(ASSETS/'textures/smoke.png',64,64,smoke)
png(ASSETS/'textures/spark.png',32,64,lambda x,y: math.exp(-5*((x-15.5)/5)**2 - 2*((y-31.5)/27)**2))
png(ASSETS/'textures/explosion.png',256,128,explosion)
png(ASSETS/'textures/flame.png',64,64,flame)
png(ASSETS/'textures/rain.png',32,64,lambda x,y: math.exp(-4*((x-15.5)/2.2)**2)*math.sin(math.pi*y/63)**.6)

def track(*keys): return [list(k) for k in keys]
def gradient(*keys): return [dict(time=t,color=list(c)) for t,c in keys]
def read(name): return json.loads((MODULE/'examples/ParticleComposition/Assets/effects'/name).read_text())
sm = read('CurvedSmoke.ayparticle')['emitters'][0]['effect']
sp = read('CurvedSparks.ayparticle')['emitters'][0]['effect']
sm.update(texturePath='textures/smoke.png', looping=True, size=[.24,.4], rate=18,
          velocityMin=[-.12,.35,0], velocityMax=[.12,.65,0], angularVelocity=[-.4,.4], rotation=[-3.14,3.14])
sm['sizeCurve']=track((0,.6),(.4,1.8),(1,3))
sp.update(texturePath='textures/spark.png', looping=True, duration=2, burst=55, rate=0,
          size=[.10,.18], gravity=[0,-2,0], lifetime=[.5,1.2])
fl = copy.deepcopy(sm)
fl.update(texturePath='textures/flame.png', blend=1, capacity=256, rate=55, lifetime=[.45,.85], size=[.25,.45],
          velocityMin=[-.1,.7,0],velocityMax=[.1,1.25,0],gravity=[0,.2,0],drag=.15,shape=1,extent=[.12,.04,0],
          rotation=[-.15,.15],angularVelocity=[-.1,.1])
fl['sizeCurve']=track((0,.5),(.2,1),(1,.25))
fl['opacityCurve']=track((0,0),(.12,.85),(.55,.65),(1,0))
fl['colorGradient']=gradient((0,(1.4,1,.3,1)),(.35,(1.5,.35,.04,1)),(1,(.5,.04,.01,0)))
rain=copy.deepcopy(sm)
rain.update(texturePath='textures/rain.png',capacity=512,rate=100,lifetime=[.7,.95],size=[.22,.32],
            shape=1,extent=[1.6,.04,0],velocityMin=[-.3,-4.5,0],velocityMax=[-.2,-4,0],
            gravity=[0,-.5,0],drag=0,rotation=[-.06,-.06],angularVelocity=[0,0])
rain['sizeCurve']=track((0,1),(1,1))
rain['opacityCurve']=track((0,0),(.05,.5),(.9,.5),(1,0))
rain['colorGradient']=gradient((0,(.65,.8,1,1)),(1,(.65,.8,1,1)))

def single(name,effect,offset=(0,0,0)):
    return dict(schemaVersion=2,name=name,duration=3,looping=False,
                emitters=[dict(name=name,delay=0,offset=list(offset),effect=effect)])
assets={'Smoke':single('Smoke',sm),'Sparks':single('Sparks',sp),
        'Flame':single('Flame',fl),'Rain':single('Rain',rain,(0,1.8,0))}
ex=read('CombinedExplosion.ayparticle')
ex.update(name='Explosion',duration=3,looping=True)
for e in ex['emitters']:
    fx=e['effect']
    if e['name']=='Smoke':
        fx.update(texturePath='textures/smoke.png',size=[.3,.5])
    elif e['name']=='Sparks':
        fx.update(texturePath='textures/spark.png',size=[.1,.15])
    else:
        fx.update(texturePath='textures/explosion.png',columns=4,rows=2,frameCount=8,framesPerSecond=12,
                  size=[1.1,1.1],lifetime=[.65,.65])
        fx['sizeCurve']=track((0,.6),(.3,1.2),(1,1.6))
        fx['opacityCurve']=track((0,1),(.45,.8),(1,0))
        fx['colorGradient']=gradient((0,(1.5,1,.3,1)),(.35,(1.4,.35,.04,1)),(1,(.5,.08,.01,0)))
assets['Explosion']=ex
for name,asset in assets.items():
    path=ASSETS/'effects'/f'{name}.ayparticle'
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(asset,indent=2)+'\n',encoding='utf-8')
scene=json.loads((MODULE/'examples/ParticleComposition/Assets/worlds/ParticleComposition.ayscene').read_text())
template=scene['entities'][1]
scene['entities']=scene['entities'][:1]
for i,name in enumerate(['Smoke','Sparks','Explosion','Flame','Rain']):
    entity=copy.deepcopy(template)
    entity.update(id=i+2,name=name)
    for component in entity['components']:
        if component['$type']=='Transform': component['position']=[(i-2)*2,0,0]
        if component['$type']=='ParticleEffectComponent': component['effectPath']=f'effects/{name}.ayparticle'
    scene['entities'].append(entity)
path=ASSETS/'worlds/ParticleLibrary.ayscene'
path.parent.mkdir(parents=True,exist_ok=True)
path.write_text(json.dumps(scene,indent=2)+'\n',encoding='utf-8')
for entity in scene['entities']:
    for component in entity['components']:
        if component['$type']=='ParticleEffectComponent': component['backend']=1
(path.parent/'ParticleLibraryGpu.ayscene').write_text(json.dumps(scene,indent=2)+'\n',encoding='utf-8')
print('Generated five effects, five original sprites and showcase scene:',ASSETS)
