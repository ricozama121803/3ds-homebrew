#!/usr/bin/env python3
"""Anatomical volumes -> watertight union -> heat-weighted 3DS skin and painted atlas."""
import bpy,math,os,sys,bmesh
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from PIL import Image
sys.path.insert(0,os.path.dirname(__file__))
from rig import BONES,blender,game
HERE=os.path.dirname(os.path.abspath(__file__))
OUT=os.environ.get('HERO_OUT',HERE);os.makedirs(OUT,exist_ok=True)
STYLES={'base':{},
 'athletic':dict(chest=1.07,waist=.86,pelvis=.86,neck=1.22,trap=1.2,delt=1.12,arm=1.08,fore=1.1,thigh=.9,quad=.9,glute=.78,calf=1.0,hand=1.2,lat=.9),
 'lean':dict(chest=.98,waist=.82,pelvis=.82,neck=1.12,trap=1.05,delt=1.0,arm=.95,fore=.98,thigh=.84,quad=.84,glute=.72,calf=.92,hand=1.15,lat=.8),
 'heroic':dict(chest=1.15,waist=.84,pelvis=.88,neck=1.3,trap=1.4,delt=1.25,arm=1.18,fore=1.18,thigh=.96,quad=.96,glute=.8,calf=1.06,hand=1.25,lat=1.0)}
K=dict(chest=1,waist=1,pelvis=1,neck=1,trap=1,delt=1,arm=1,fore=1,thigh=1,quad=1,glute=1,calf=1,hand=1,lat=1);K.update(STYLES[os.environ.get('HERO_STYLE','base')])
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
parts=[]
def ell(c,r):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=24,ring_count=16,location=blender(c))
 o=bpy.context.object;o.scale=(r[0],r[2],r[1]);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);parts.append(o)
def E(c,r,k):return ell(c,(r[0]*k,r[1],r[2]*k))
def limb(a,b,ra,rb):
 a,b=Vector(a),Vector(b);d=(b-a).normalized();x=d.cross(Vector((0,0,1))).normalized();z=x.cross(d);verts=[];faces=[];N=16
 for j in range(7):
  t=j/6;r=(ra*(1-t)+rb*t)*(1+.09*math.sin(t*math.pi));c=a.lerp(b,t)
  for k in range(N):
   th=k*math.tau/N;verts.append(blender(c+x*(r*math.cos(th))+z*(r*.93*math.sin(th))))
 for j in range(6):
  for k in range(N):a0=j*N+k;b0=j*N+(k+1)%N;faces.append((a0,b0,b0+N,a0+N))
 faces.extend([tuple(reversed(range(N))),tuple(range(6*N,7*N))])
 me=bpy.data.meshes.new('anatomy');me.from_pydata(verts,[],faces);me.update();o=bpy.data.objects.new('volume',me);bpy.context.collection.objects.link(o);parts.append(o)
 ell(a,(ra,ra,ra*.93));ell(b,(rb,rb,rb*.93))
def cr(pts,n):                                                    # Catmull-Rom through control rows (each row a tuple), n samples
 P=np.array(pts,float);out=[];m=len(P)
 for i in range(n):
  t=i/(n-1)*(m-1);k=min(int(t),m-2);f=t-k;p0=P[max(k-1,0)];p1=P[k];p2=P[k+1];p3=P[min(k+2,m-1)]
  out.append(.5*((2*p1)+(-p0+p2)*f+(2*p0-5*p1+4*p2-p3)*f*f+(-p0+3*p1-3*p2+p3)*f**3))
 return np.array(out)
def solid(verts,faces):
 me=bpy.data.meshes.new('anatomy');me.from_pydata(verts,[],faces);me.update()
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()
 o=bpy.data.objects.new('volume',me);bpy.context.collection.objects.link(o);parts.append(o)
def torso_loft(prof,N=32):                                        # rows: (y, half width, half depth, z centre, squareness: 2 = ellipse, higher = boxier)
 rows=cr(prof,38);verts=[];faces=[]
 for y,a,b,c,e in rows:
  for k in range(N):
   th=k*math.tau/N;cs,sn=math.cos(th),math.sin(th);q=2.0/max(e,2.0);verts.append(blender((a*math.copysign(abs(cs)**q,cs),y,c+b*math.copysign(abs(sn)**q,sn))))
 R=len(rows)
 for j in range(R-1):
  for k in range(N):a0=j*N+k;b0=j*N+(k+1)%N;faces.append((a0,b0,b0+N,a0+N))
 faces.append(tuple(reversed(range(N))));faces.append(tuple(range((R-1)*N,R*N)));solid(verts,faces)
def tube(a,b,prof,flat=.93,N=18,R=22):                            # prof rows: (t, radius); tapered limb whose start may sit inside the torso
 a,b=Vector(a),Vector(b);d=(b-a).normalized();x=d.cross(Vector((0,0,1)));x=x.normalized() if x.length>1e-4 else Vector((1,0,0));z=x.cross(d);verts=[];faces=[]
 rows=cr([(t,r) for t,r in prof],R)
 for t,r in rows:
  c=a.lerp(b,min(max(t,0),1))
  for k in range(N):
   th=k*math.tau/N;verts.append(blender(c+x*(r*math.cos(th))+z*(r*flat*math.sin(th))))
 for j in range(R-1):
  for k in range(N):a0=j*N+k;b0=j*N+(k+1)%N;faces.append((a0,b0,b0+N,a0+N))
 faces.append(tuple(reversed(range(N))));faces.append(tuple(range((R-1)*N,R*N)));solid(verts,faces)
def foot_loft(cx,prof,N=24):                                      # rows: (z, half width, y top, y bottom, squareness); flat sole, instep sloping to a narrow toe
 rows=cr(prof,22);verts=[];faces=[]
 for z,w,yt,yb,e in rows:
  for k in range(N):
   th=k*math.tau/N;cs,sn=math.cos(th),math.sin(th);q=2.0/max(e,2.0);verts.append(blender((cx+w*math.copysign(abs(cs)**q,cs),(yt+yb)/2+(yt-yb)/2*math.copysign(abs(sn)**q,sn),z)))
 R=len(rows)
 for j in range(R-1):
  for k in range(N):a0=j*N+k;b0=j*N+(k+1)%N;faces.append((a0,b0,b0+N,a0+N))
 faces.append(tuple(reversed(range(N))));faces.append(tuple(range((R-1)*N,R*N)));solid(verts,faces)
# hips, waist, ribs, chest, shoulder line, trapezius, neck as one flowing profile
torso_loft([(-.20,.100,.080,-.010,2.2),(-.13,.138,.098,-.016,2.3),(-.07,.172,.120,-.020,2.4),(.00,.176,.120,-.014,2.4),(.09,.170,.111,-.007,2.4),(.19,.164,.105,0,2.4),(.29,.192,.114,.002,2.8),(.39,.232,.127,.006,4.0),(.47,.252,.130,.006,4.6),(.525,.254,.112,-.002,4.0),(.560,.214,.095,-.006,3.2),(.600,.140,.088,-.005,2.3),(.655,.092,.080,-.002,2.0),(.72,.082,.074,0,2.0)])
torso_loft([(.700,.040,.046,.040,2.4),(.735,.074,.068,.032,3.0),(.772,.098,.086,.022,3.4),(.820,.103,.094,.010,3.0),(.870,.105,.099,.004,2.7),(.920,.100,.097,0,2.4),(.960,.076,.082,0,2.2),(.985,.034,.040,0,2.0)],N=36)
E((0,.882,.072),(.088,.02,.04),1);E((.052,.805,.07),(.03,.03,.03),1);E((-.052,.805,.07),(.03,.03,.03),1)                  # brow ridge, cheekbones;E((0,.665,0),(.082,.12,.076),1)
for s in [-1,1]:
 def p(x,y,z=0):return(s*x,y,z)
 E(p(.115,.435,.056),(.122,.072,.07),1)                                               # pecs
 tube(p(.215,.51,-.004),p(.468,.292),[(0,.07),(.14,.078),(.32,.079),(.65,.061),(1,.05)])           # shoulder cap + deltoid + upper arm, rooted inside the chest
 E(p(.468,.292),(.054,.054,.054),1)
 tube(p(.468,.292),p(.655,.07,.015),[(0,.052),(.22,.061),(.55,.047),(1,.032)])                      # forearm: bulge below the elbow, slim wrist
 limb(p(.655,.07,.015),p(.74,-.04,.03),.04,.037);limb(p(.668,.04,.045),p(.69,-.03,.08),.021,.017)
 tube(p(.112,.00,-.022),p(.122,-.515,.015),[(0,.086),(.15,.088),(.45,.076),(1,.056)],flat=1.1)         # thigh rooted in the pelvis, tapering to the knee
 E(p(.125,-.515,.012),(.055,.055,.055),1)
 tube(p(.125,-.515,.015),p(.125,-1.02),[(0,.055),(.18,.063),(.5,.056),(1,.042)])                    # shin with a calf, down to the ankle
 E(p(.125,-.72,-.028),(.05,.12,.056),1);foot_loft(s*.125,[(-.062,.043,-1.066,-1.142,2.6),(-.035,.047,-1.000,-1.146,3.0),(.02,.050,-1.014,-1.148,3.2),(.09,.055,-1.064,-1.148,3.2),(.16,.058,-1.098,-1.148,3.0),(.21,.054,-1.112,-1.148,2.8),(.245,.044,-1.120,-1.148,2.6),(.262,.030,-1.126,-1.147,2.3)])
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();body=bpy.context.object;body.name='hero_body';bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
mod=body.modifiers.new('Watertight union','REMESH');mod.mode='VOXEL';mod.voxel_size=.012;mod.use_smooth_shade=True;bpy.ops.object.modifier_apply(modifier=mod.name)
mod=body.modifiers.new('Anatomical transitions','SMOOTH');mod.factor=1.;mod.iterations=3;bpy.ops.object.modifier_apply(modifier=mod.name)
body.data.calc_loop_triangles();n=len(body.data.loop_triangles)
def clean(me):
 bm=bmesh.new();bm.from_mesh(me);ok=all(e.is_manifold for e in bm.edges);bm.free();return ok
dense=body.data.copy()
for sym,tgt in ((1,2780),(1,2700),(1,2860),(0,2780),(0,2700),(0,2860),(0,2600),(0,2950)):
 body.data=dense.copy();mod=body.modifiers.new('3DS budget','DECIMATE');mod.ratio=tgt/n;mod.use_symmetry=bool(sym);mod.symmetry_axis='X';bpy.ops.object.modifier_apply(modifier=mod.name)
 mod=body.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
 bm=bmesh.new();bm.from_mesh(body.data);bmesh.ops.remove_doubles(bm,verts=bm.verts,dist=1e-5);bm.to_mesh(body.data);bm.free()
 if clean(body.data):break
for p in body.data.polygons:p.use_smooth=True
bm=bmesh.new();bm.from_mesh(body.data);bm.verts.ensure_lookup_table()
assert all(e.is_manifold for e in bm.edges),'nonmanifold skin'
seen=set();todo=[bm.verts[0]]
while todo:
 v=todo.pop()
 if v in seen:continue
 seen.add(v);todo.extend(e.other_vert(v) for e in v.link_edges)
assert len(seen)==len(bm.verts),'disconnected body'
bm.free()
armdata=bpy.data.armatures.new('Hero skeleton');arm=bpy.data.objects.new('hero_rig',armdata);bpy.context.collection.objects.link(arm)
bpy.context.view_layer.objects.active=arm;body.select_set(False);arm.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
for name,parent,h,t in BONES:
 b=armdata.edit_bones.new(name);b.head=blender(h);b.tail=blender(t)
 if parent>=0:b.parent=armdata.edit_bones[BONES[parent][0]]
bpy.ops.object.mode_set(mode='OBJECT');body.select_set(True);bpy.ops.object.parent_set(type='ARMATURE_AUTO')
bpy.context.view_layer.objects.active=body;arm.select_set(False)
bpy.ops.object.vertex_group_limit_total(limit=4);bpy.ops.object.vertex_group_normalize_all(lock_active=False)
for v in body.data.vertices:assert 0<len(v.groups)<=4,'missing weights'
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=1.3,island_margin=.018);bpy.ops.object.mode_set(mode='OBJECT')
me=body.data;bvh=BVHTree.FromPolygons([v.co for v in me.vertices],[p.vertices[:] for p in me.polygons],all_triangles=True);ao=me.attributes.new('suit_ao','FLOAT','POINT')
for v in me.vertices:
 n=v.normal.normalized();t=n.cross(Vector((0,0,1)))
 if t.length<.01:t=n.cross(Vector((0,1,0)))
 t.normalize();bt=n.cross(t);occ=0
 for j in range(24):
  z=(j+.5)/24;a=j*2.399963;r=math.sqrt(1-z*z);d=n*z+t*(r*math.cos(a))+bt*(r*math.sin(a));hit=bvh.ray_cast(v.co+n*.002,d,.3)
  if hit[0] is not None:occ+=1-hit[3]/.3
 ao.data[v.index].value=1-.42*occ/24
# Object-space paint is rasterized into the UV atlas, so patterns meet across seams.
RED=np.array((138.,16.,30.));NAVY=np.array((20.,26.,56.));SILVER=np.array((180.,180.,188.));BLACK=np.array((10.,11.,18.))
AA=.0022;WD=.0049
def cov(d,w=WD,aa=AA):return np.clip((w*.5-d)/aa+.5,0,1)                  # antialiased line coverage from a distance in world units
def segd(P2,A,B):                                                          # distance from points (n,2) to segment A-B
 A=np.array(A);B=np.array(B);ab=B-A;t=np.clip(((P2-A)@ab)/(ab@ab),0,1);return np.linalg.norm(P2-(A+t[:,None]*ab),axis=1)
def webdist(P,arm,side):
 x,y,z=P.T;sp=.058
 u=x/sp;v=(y+.011*np.cos(x*34))/sp
 grid=np.minimum(np.abs(u-np.round(u))*sp,np.abs(v-np.round(v))*sp)
 cy=.5;r=np.hypot(x,y-cy)+1e-6;ai=np.arctan2(x,y-cy)*(18/(2*np.pi));rg=(r+.014*np.cos(ai*2*np.pi))/sp
 polar=np.minimum(np.abs(ai-np.round(ai))*(2*np.pi/18)*r,np.abs(rg-np.round(rg))*sp)
 polar=np.where(r<.05,1.0,polar)                                            # no knot of strands at the web's centre
 ang=np.arctan2(x,z)*(12/(2*np.pi));hv=y/.04
 head=np.minimum(np.abs(ang-np.round(ang))*.05,np.abs(hv-np.round(hv))*.04)
 d=np.array((1.,-1.15,0));d/=np.linalg.norm(d);m=np.where(side>0,1.,-1.)
 dd=np.stack((d[0]*m,np.full_like(m,d[1]),np.zeros_like(m)),1);q=P-np.stack((.265*m,np.full_like(m,.53),np.zeros_like(m)),1)
 sd=(q*dd).sum(1);e2=np.stack((np.zeros_like(m),np.zeros_like(m),np.ones_like(m)),1);e3=np.cross(dd,e2)
 th=np.arctan2((q*e3).sum(1),(q*e2).sum(1))*(8/(2*np.pi));rr=sd/.05
 armd=np.minimum(np.abs(th-np.round(th))*.05,np.abs(rr-np.round(rr))*.05)
 return np.where(y>.70,head,np.where(arm,armd,np.where(y>-.2,polar,grid)))
LEGS=(((.010,.014),(.040,.050),(.075,.046)),((.012,.006),(.050,.022),(.086,.002)),((.012,-.004),(.050,-.020),(.082,-.050)),((.010,-.012),(.040,-.050),(.060,-.088)))
def spider(P,cx,cy,sc,zs):                                                 # coverage of a spider emblem facing +z (zs=1) or -z (zs=-1)
 x,y,z=P.T;ok=(z*zs>.05)&(np.abs(y-cy)<.12*sc)&(np.abs(x-cx)<.12*sc)
 p2=np.stack((np.abs(x-cx),y-cy),1);c=np.zeros(len(y))
 th=((p2[:,0]/(.011*sc))**2+((p2[:,1]-.014*sc)/(.015*sc))**2);ab=((p2[:,0]/(.015*sc))**2+((p2[:,1]+.02*sc)/(.026*sc))**2)
 c=np.maximum(c,np.clip((1-th)/.25+.5,0,1));c=np.maximum(c,np.clip((1-ab)/.25+.5,0,1))
 for leg in LEGS:
  dist=np.minimum(segd(p2,np.array(leg[0])*sc,np.array(leg[1])*sc),segd(p2,np.array(leg[1])*sc,np.array(leg[2])*sc))
  c=np.maximum(c,cov(dist,.0048*sc,AA))
 return c*ok
def paint_v(P,N):
 x,y,z=P.T;ax=np.abs(x);ny=N[:,1];n=len(y)
 arm=(ax>.215)&(y<.62)&((y>.12)|(ax>.42));front=z>=0
 vw=np.clip(.05+(y-.115)*.62,0,.27)
 chest=(y>.115)&(ax<vw);back=(~front)&(y>.115)&(ax<.2);shoulders=(y>.50)&(ax<.30)
 torso_red=chest|(back&(y>.2))|shoulders
 belt=(y>.025)&(y<=.115)&(ax<.25);boot=(y<-.70);head=y>.70;hand=ax>.63
 armnavy=arm&(ny<-.22)&(ax<.52)&(ax>.235)
 red=head|boot|hand|belt|torso_red|(arm&~armnavy)
 col=np.where(red[:,None],RED,NAVY)
 # painted muscle definition: soft, so it reads as form rather than lines
 mu=np.ones(n);fr=front&(y>.10)&(y<.62)
 def soft(m,d,w,k):return np.where(m,1-(1-k)*np.clip(1-d/w,0,1)**1.5,1)
 ype=.372+.06*np.clip(ax/.2,0,1)
 mu*=soft(fr&(ax>.015)&(ax<.23),np.abs(y-ype),.011,.58)
 mu*=soft(fr&(y>.36)&(y<.54),ax,.007,.62)
 mu*=soft(fr&(ax>.03)&(ax<.2),np.abs(y-(.552-.02*ax/.2)),.008,.68)
 for ya in (.175,.23,.285):mu*=soft(fr&(ax<.085),np.abs(y-ya),.008,.64)
 mu*=soft(fr&(y>.13)&(y<.34),ax,.007,.64)
 bk=(~front)&(y>.12)&(y<.58)
 mu*=soft(bk,ax,.008,.62);mu*=soft(bk&(ax>.03)&(ax<.2),np.abs(y-(.46-.5*(ax-.03))),.008,.7)
 mu*=soft(fr&(ax>.2)&(ax<.27)&(y>.35)&(y<.56),np.abs(ax-.205-.02*(y-.4)),.007,.68)
 mu*=1+.10*np.clip(ny,-1,1)*(y>.5)
 col=col*(( .93+.07*np.clip(N[:,2],-1,1))*mu)[:,None]
 # the web: antialiased silver strands over the red only
 wc=cov(webdist(P,arm,x))*red
 bc=np.where(belt,np.maximum(cov(np.abs(y-.045),.0065),cov(np.abs(y-.095),.0065)),0)
 sil=SILVER*(.88+.12*np.clip(N[:,2],-1,1))[:,None]*np.where(mu<.95,.82+.18*mu,1)[:,None]
 w_=np.maximum(wc,bc);col=col*(1-w_[:,None])+sil*w_[:,None]
 cuff=np.maximum(cov(np.abs(y+.70),.008)*(ax<.3),cov(np.abs(y-.115),.006)*(ax<.25))
 col=col*(1-cuff[:,None])+(SILVER*.85)*cuff[:,None]
 for sc,zs,cy in ((.95,1,.485),(1.8,-1,.40)):
  c=spider(P,0,cy,sc,zs);col=col*(1-c[:,None])+BLACK*c[:,None]
 # lenses: large, white, angled toward the nose, with a dark rim
 ex=(ax-.048)/.043;ey=(y-(.859+(ax-.048)*.45))/.026;lens=ex**2+ey**2;eye=((y>.785)&(z>.035)).astype(float)
 rim=np.clip((1.7-lens)/.25+.5,0,1)*eye;fill=np.clip((.95-lens)/.2+.5,0,1)*eye
 col=col*(1-rim[:,None])+BLACK*rim[:,None];col=col*(1-fill[:,None])+np.array((240.,242.,238.))*fill[:,None]
 return np.clip(col,0,255)
W=2048;pixels=np.zeros((W,W,3));mask=np.zeros((W,W),bool);uv=me.uv_layers.active.data
for poly in me.polygons:
 ids=list(poly.loop_indices);tex=np.array([uv[i].uv[:] for i in ids])*(W-1);vs=[me.vertices[me.loops[i].vertex_index] for i in ids];pos=np.array([game(v.co) for v in vs]);nrm=np.array([game(v.normal) for v in vs])
 lo=np.maximum(0,np.floor(tex.min(0)).astype(int));hi=np.minimum(W-1,np.ceil(tex.max(0)).astype(int));A=np.column_stack((tex[1]-tex[0],tex[2]-tex[0]))
 if abs(np.linalg.det(A))<1e-7:continue
 inv=np.linalg.inv(A);gx,gy=np.meshgrid(np.arange(lo[0],hi[0]+1),np.arange(lo[1],hi[1]+1));bb=np.stack((gx.ravel()-tex[0][0],gy.ravel()-tex[0][1]),1)@inv.T
 w=np.column_stack((1-bb.sum(1),bb));ok=w.min(1)>=-.01
 if ok.any():
  pixels[gy.ravel()[ok],gx.ravel()[ok]]=paint_v(w[ok]@pos,w[ok]@nrm);mask[gy.ravel()[ok],gx.ravel()[ok]]=True
for _ in range(10):
 old=mask.copy();src=pixels.copy()
 for dy,dx in [(1,0),(-1,0),(0,1),(0,-1)]:
  m=np.roll(old,(dy,dx),(0,1))&~mask;pixels[m]=np.roll(src,(dy,dx),(0,1))[m];mask[m]=True
Image.fromarray(np.flipud(pixels).astype('uint8')).resize((1024,1024),Image.Resampling.BOX).save(os.path.join(OUT,'hero_atlas.png'))
mat=bpy.data.materials.new('Continuous suit');mat.use_nodes=True;nodes=mat.node_tree.nodes;bs=nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.7;tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(os.path.join(OUT,'hero_atlas.png'));mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color']);body.data.materials.clear();body.data.materials.append(mat)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'hero.blend'))
print('Continuous body:',len(me.vertices),'vertices,',len(me.polygons),'triangles;',len(BONES),'bones; manifold, connected, <=4 weights')
