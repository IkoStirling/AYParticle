"""Original procedural leaf flipbook + schema-3 composite and CPU 3D assets.
Run with Python standard library. Eight frames show a rotating front/back leaf.
"""
from pathlib import Path
import copy,json,math,struct,zlib
MODULE=Path(__file__).resolve().parents[1]
ROOT=MODULE/'examples/FallingLeaves'
ASSETS=ROOT/'Assets'
def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
def pixel(x,y):
    frame=x//64+4*(y//64);theta=frame*math.pi/4
    u=(x%64-31.5)/26;v=(y%64-31.5)/27
    width=max(.09,abs(math.cos(theta)))
    leafx=(u-.09*math.sin(v*4+theta))/width
    leafwidth=.55*(1-v*v)*(1+.12*math.sin(v*15))
    edge=min((leafwidth-abs(leafx))*.9,(1-abs(v))*.8)
    alpha=max(0,min(1,edge*24))
    if alpha==0:return (0,0,0,0)
    vein=max(0,1-abs(leafx)*24)
    shade=.72+.28*math.cos(theta)
    back=math.cos(theta)<0
    base=(178,128,43) if back else (232,173,58)
    color=[round(min(255,c*shade+vein*20)) for c in base]
    return (*color,round(alpha*255))
rows=bytearray()
for y in range(128):
    rows.append(0)
    for x in range(256):rows.extend(pixel(x,y))
(ASSETS/'textures').mkdir(parents=True,exist_ok=True)
(ASSETS/'textures/leaf-flipbook.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',256,128,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))
def number(mode=2,scope=2,lo=0,hi=0,value=0,curve=None,upper=None):
    return dict(mode=mode,scope=scope,value=value,min=lo,max=hi,choices=[],curve=curve or [],upperCurve=upper or [])
def boolean(prob=.5,scope=2):return dict(mode=1,scope=scope,value=False,probability=prob)
effect=json.loads((MODULE/'examples/ParticleLibrary/Assets/effects/Smoke.ayparticle').read_text())['emitters'][0]['effect']
effect.update(capacity=512,burst=0,rate=24,duration=6,seed=17,looping=True,dimension=0,shape=1,space=1,blend=0,backend=2,
    extent=[2,.1,0],velocityMin=[-.12,-.5,0],velocityMax=[.12,-.25,0],gravity=[0,-.08,0],drag=.12,
    lifetime=[3,5],size=[.18,.35],rotation=[-math.pi,math.pi],angularVelocity=[-.7,.7],endSizeScale=1,
    startColor=[1,1,1,1],endColor=[1,1,1,0],sizeCurve=[[0,1],[1,1]],opacityCurve=[[0,0],[.12,.85],[.8,.85],[1,0]],
    colorGradient=[],texturePath='textures/leaf-flipbook.png',columns=4,rows=2,firstFrame=0,frameCount=8,framesPerSecond=8)
effect['extensions']=dict(properties=dict(animationFps=number(lo=5,hi=11),animationOffset=number(lo=0,hi=7),
    swayAmplitude=number(lo=.12,hi=.45),swayFrequency=number(lo=.4,hi=.9),swayPhase=number(lo=0,hi=math.tau)),
    flipX=boolean(),flipY=dict(mode=0,scope=2,value=False,probability=.5),reverseAnimation=boolean(),animationPlayback=0,
    animationOverLifetime=False,facing=0,tiltX=[0,0],tiltY=[0,0],tumbleX=[0,0],tumbleY=[0,0])
def layer(name,id,e,offset,delay=0,kind=0):return dict(name=name,id=id,kind=kind,offset=offset,delay=delay,
    enabled=dict(mode=0,scope=0,value=True,probability=.5),delayValue=number(mode=0),effect=e)
main=layer('Foreground leaves',1,effect,[0,1.8,0])
distant=layer('Distant leaves',2,copy.deepcopy(effect),[0,2.4,0],.4)
distant['effect'].update(rate=9,size=[.08,.15],lifetime=[4,5.5])
distant['enabled']=boolean(.8,1);distant['delayValue']=number(scope=1,lo=.1,hi=.6)
hero=layer('Hero leaf',3,copy.deepcopy(effect),[-1,0,0],1,1);hero['effect'].update(size=[.7,.7],lifetime=[4.5,4.5],angularVelocity=[.6,1.1])
hero['effect']['extensions']['properties']['swayAmplitude']=number(lo=.05,hi=.1)
asset=dict(schemaVersion=3,name='Falling Leaves',seed=2026,duration=6,looping=True,backend=2,emitters=[main,distant,hero])
(ASSETS/'effects').mkdir(parents=True,exist_ok=True)
(ASSETS/'effects/FallingLeaves.ayparticle').write_text(json.dumps(asset,indent=2)+'\n')
three=copy.deepcopy(asset);three['name']='Tumbling Leaves 3D';three['backend']=0
for l in three['emitters']:
    e=l['effect'];e.update(dimension=1,shape=3,extent=[2,.1,.7],velocityMin=[-.12,-.5,-.1],velocityMax=[.12,-.25,.1])
    e['extensions'].update(facing=1,tiltX=[-.5,.5],tiltY=[-math.pi,math.pi],tumbleX=[-.5,.5],tumbleY=[1.2,2.4])
(ASSETS/'effects/TumblingLeaves3D.ayparticle').write_text(json.dumps(three,indent=2)+'\n')
scene=json.loads((MODULE/'examples/ParticleLibrary/Assets/worlds/ParticleLibraryGpu.ayscene').read_text())
scene['entities']=scene['entities'][:2]
scene['entities'][1]['name']='Falling Leaves'
scene['entities'][1]['components'][0]['position']=[0,0,0]
scene['entities'][1]['components'][1]['effectPath']='effects/FallingLeaves.ayparticle'
scene['entities'][1]['components'][1]['backend']=3
(ASSETS/'worlds').mkdir(parents=True,exist_ok=True)
(ASSETS/'worlds/FallingLeaves.ayscene').write_text(json.dumps(scene,indent=2)+'\n')
profile=json.loads((MODULE/'examples/ParticleLibrary/BuildProfiles/windows.aybuild.json').read_text())
profile['id']='falling-leaves-windows';profile['content']['assetRoot']='AYRuntime/AYParticle/examples/FallingLeaves/Assets'
profile['cache']['root']='out/particle-leaves/cache';profile['package']['output']='out/particle-leaves/package';profile['run']['arguments']=['--leaves','--gpu']
(ROOT/'BuildProfiles').mkdir(parents=True,exist_ok=True)
(ROOT/'BuildProfiles/windows.aybuild.json').write_text(json.dumps(profile,indent=2)+'\n')
print('Generated original leaf flipbook, composite, CPU 3D variant, scene and build profile')
