#!/usr/bin/env python3
"""CMU BVH -> retargeted local joint quaternions. No foot-path synthesis.
BVH channel order is honored; source FK gives anatomical segment frames.
The common-axis inverse bind is shared with the skin exporter.
"""
import os,sys,re,math,json,hashlib
import bpy
import numpy as np
from mathutils import Matrix,Vector,Quaternion
sys.path.insert(0,os.path.dirname(__file__))
from rig_otto import BONES, LEG_LEN, SOLE, HEEL, TOE
HERE=os.path.dirname(os.path.abspath(__file__))
def read_bvh(path):
 text=open(path).read();hier,motion=text.split('MOTION');tok=re.findall(r'[{}]|[^\s{}]+',hier);joints=[];pos=2;channel=0
 def joint(parent):
  nonlocal pos,channel
  name=tok[pos];pos+=1;assert tok[pos]=='{';pos+=1;i=len(joints);j={'name':name,'parent':parent,'offset':None,'channels':[]};joints.append(j)
  while tok[pos]!='}':
   op=tok[pos];pos+=1
   if op=='OFFSET':j['offset']=Vector(map(float,tok[pos:pos+3]));pos+=3
   elif op=='CHANNELS':
    n=int(tok[pos]);pos+=1;j['channels']=[(c,channel+k) for k,c in enumerate(tok[pos:pos+n])];channel+=n;pos+=n
   elif op=='JOINT':joint(i)
   elif op=='End':
    pos+=1;assert tok[pos]=='{';pos+=1;assert tok[pos]=='OFFSET';pos+=1;j['end']=Vector(map(float,tok[pos:pos+3]));pos+=4
   else:raise ValueError(op)
  pos+=1
 joint(-1);lines=motion.strip().splitlines();dt=float(lines[1].split(':')[1]);data=np.array([list(map(float,l.split())) for l in lines[2:]])
 positions=[];rotations=[]
 for row in data:
  pp=[];rr=[]
  for j in joints:
   r=Matrix.Identity(3);t=j['offset'].copy()
   for name,c in j['channels']:
    if name.endswith('rotation'):r=r@Matrix.Rotation(math.radians(row[c]),3,name[0])
    else:t['XYZ'.index(name[0])]+=row[c]
   par=j['parent'];pp.append(t if par<0 else pp[par]+rr[par]@t);rr.append(r if par<0 else rr[par]@r)
  positions.append(pp);rotations.append(rr)
 return joints,dt,positions,rotations

def frame(d,x):
 y=Vector(d).normalized();x=Vector(x);x=(x-y*x.dot(y)).normalized();z=x.cross(y);return Matrix((x,y,z)).transposed()
def inspect():
 for file in ['16_15','16_35','09_01']:
  js,dt,pp,rr=read_bvh(os.path.join(HERE,'mocap',file+'.bvh'));names={j['name']:i for i,j in enumerate(js)};a=np.array([[tuple(v) for v in f] for f in pp]);foot=a[:,names['LeftFoot']];rel=foot-a[:,0];peaks=[i for i in range(6,len(a)-6) if rel[i,2]==max(rel[i-5:i+6,2])]
  print(file,'peaks',peaks,'left ankle range',foot[5:,1].min(),foot[5:,1].max(),'rooty',a[5:,0,1].min(),a[5:,0,1].max())
  for i in peaks:print(i,'root',a[i,0], 'footrel',rel[i])

# Cycles begin in left support, after the calibration/dropout frames.
CLIPS=[('walk','16_15',145,283),('jog','16_35',15,110),('sprint','09_01',6,92)]
N=48

def export():
 output=[];report=[]
 for label,file,start,end in CLIPS:
  path=os.path.join(HERE,'mocap',file+'.bvh');js,dt,pp,rr=read_bvh(path);ids={j['name']:i for i,j in enumerate(js)}
  def pos(f,n):return pp[f][ids[n]]
  leg=(pos(0,'LeftLeg')-pos(0,'LeftUpLeg')).length+(pos(0,'LeftFoot')-pos(0,'LeftLeg')).length;scale=LEG_LEN/leg
  delta=pos(end,'Hips')-pos(start,'Hips');heading=math.atan2(delta.x,delta.z);unheading=Matrix.Rotation(-heading,3,'Y');distance=Vector((delta.x,0,delta.z)).length*scale
  frames=[];source_heights=[]
  for f in range(start,end+1):
   left=(pos(f,'LeftArm')-pos(f,'RightArm')).normalized();root=unheading@rr[f][0];globalrot=[root]
   # Anatomical torso direction eliminates converter-specific spine rest offsets.
   for a,b in [('LowerBack','Spine'),('Spine','Neck1'),('Neck1','Head')]:globalrot.append(unheading@frame(pos(f,b)-pos(f,a),left))
   globalrot.append(unheading@rr[f][ids['Head']]@rr[0][ids['Head']].transposed())
   for side in ['Left','Right']:
    pairs=[(side+'Shoulder',side+'Arm'),(side+'Arm',side+'ForeArm'),(side+'ForeArm',side+'Hand'),(side+'Hand',side+'HandIndex1'),(side+'UpLeg',side+'Leg'),(side+'Leg',side+'Foot'),(side+'Foot',side+'ToeBase'),(side+'ToeBase',None)]
    for a,b in pairs:
     bi=len(globalrot);rest=frame(Vector(BONES[bi][3])-Vector(BONES[bi][2]),(1,0,0));direction=(pos(f,b)-pos(f,a)) if b else rr[f][ids[a]]@js[ids[a]]['end']
     if direction.length<.001:direction=rr[f][ids[a]]@Vector((1 if side=='Left' else -1,0,0))
     globalrot.append(unheading@frame(direction,rr[f][0]@Vector((1,0,0)))@rest.transposed())
   local=[]
   for i,g in enumerate(globalrot):
    par=BONES[i][1];local.append((g if par<0 else globalrot[par].transposed()@g).to_quaternion().normalized())
   frames.append(local);source_heights.append(pos(f,'Hips').y*scale)
  keys=[]
  for k in range(N):
   t=k*(end-start)/N;a=int(t);b=min(a+1,len(frames)-1);t-=a
   qs=[frames[a][i].slerp(frames[b][i],t) for i in range(len(BONES))]
   # Distribute measured endpoint drift over the cycle; preserve captured timing.
   for i in range(len(BONES)):
    correction=frames[-1][i].rotation_difference(frames[0][i]);qs[i]=qs[i]@Quaternion().slerp(correction,k/N)
   h=source_heights[a]*(1-t)+source_heights[b]*t
   keys.append([qs,h])
  # Retargeted FK, including the shoe sole, determines ground offset once per clip.
  # No frame-by-frame foot path or leg IK is baked into the angle data.
  roots=[];soles=[]
  for qs,h in keys:
   gp=[];gr=[]
   for i,q in enumerate(qs):
    par=BONES[i][1];r=q.to_matrix();p=Vector((0,h,0)) if par<0 else gp[par]+gr[par]@(Vector(BONES[i][2])-Vector(BONES[par][2]));gr.append(r if par<0 else gr[par]@r);gp.append(p)
   feet=[]
   for i in [11,19]:
    feet.append(min((gp[i]+gr[i]@Vector((0,SOLE,HEEL))).y,(gp[i]+gr[i]@Vector((0,SOLE,TOE))).y))
   roots.append(h);soles.append(feet)
  floor=float(np.percentile(np.min(soles,axis=1),12));roots=np.array(roots)-floor
  # Contact flag from the retargeted heel/toe height; used only for bounded correction.
  contact=np.array(soles)-floor<.045
  output.append((label,distance,dt*(end-start),keys,roots,contact))
  report.append(dict(clip=label,source=file,frames=[start,end],frame_index='zero-based including calibration frame',duration=dt*(end-start),stride_model_units=distance,source_speed_model_units=distance/(dt*(end-start)),sha256=hashlib.sha256(open(path,'rb').read()).hexdigest()))
 lines=['// Generated from CMU BVH by assets/hero/import_mocap.py; see mocap/README.md.','#pragma once',f'#define GAIT_KEYS {N}','typedef struct { float q[OTTO_NB][4],height; unsigned char contact[2]; } GaitKey;','typedef struct { float stride,duration; GaitKey key[GAIT_KEYS]; } GaitClip;','static const GaitClip gait_clips[3] = {']
 for label,distance,duration,keys,roots,contacts in output:
  lines.append(f'{{{distance:.6f}f,{duration:.6f}f,{{ // {label}')
  for k,(qs,h) in enumerate(keys):
   rows=['{'+','.join(f'{v:.7f}f' for v in q)+'}' for q in qs]
   lines.append('{{'+','.join(rows)+'},'+f'{roots[k]:.6f}f,'+'{'+','.join(str(int(v)) for v in contacts[k])+'}},')
  lines.append('}},')
 lines+=['};'];open(os.path.join(HERE,'../../source/otto_anim.h'),'w').write('\n'.join(lines)+'\n');open(os.path.join(HERE,'mocap/cycles.json'),'w').write(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':export()
