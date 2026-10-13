"""Bind skeleton in game axes. Common-axis frames; inverse bind = translation(-head)."""
BONES=[('pelvis',-1,(0,0,0),(0,.18,0)),('spine',0,(0,.18,0),(0,.34,0)),('chest',1,(0,.34,0),(0,.56,0)),('neck',2,(0,.56,0),(0,.73,0)),('head',3,(0,.73,0),(0,.99,0))]
for side,s in [('L',1),('R',-1)]:
 def p(x,y,z=0):return(s*x,y,z)
 i=len(BONES)
 BONES += [(side+'_clavicle',2,p(.04,.54),p(.265,.53)),(side+'_upperarm',i,p(.265,.53),p(.465,.295)),(side+'_forearm',i+1,p(.465,.295),p(.655,.07,.015)),(side+'_hand',i+2,p(.655,.07,.015),p(.73,-.035,.03))]
 i=len(BONES)
 BONES += [(side+'_thigh',0,p(.125,0),p(.125,-.515,.015)),(side+'_shin',i,p(.125,-.515,.015),p(.125,-1.02,0)),(side+'_foot',i+1,p(.125,-1.02),p(.125,-1.09,.145)),(side+'_toe',i+2,p(.125,-1.09,.145),p(.125,-1.09,.235))]
def blender(p):return(p[0],-p[2],p[1])
def game(p):return(p[0],p[2],-p[1])
