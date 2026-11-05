import numpy as np, math
def rot(axis, deg):
    a=math.radians(deg); c,s=math.cos(a),math.sin(a)
    x,y,z=axis
    return np.array([[c+x*x*(1-c), x*y*(1-c)-z*s, x*z*(1-c)+y*s,0],
                     [y*x*(1-c)+z*s, c+y*y*(1-c), y*z*(1-c)-x*s,0],
                     [z*x*(1-c)-y*s, z*y*(1-c)+x*s, c+z*z*(1-c),0],[0,0,0,1]])
def trans(t):
    m=np.eye(4); m[:3,3]=t; return m
def persp(fovy,aspect,n,f):
    t=1/math.tan(math.radians(fovy)/2)
    return np.array([[t/aspect,0,0,0],[0,t,0,0],[0,0,(f+n)/(n-f),2*f*n/(n-f)],[0,0,-1,0]])
POS=np.array([12778.0,12678.0,579.0]); ANG=(-96.5,0.0,-36.0)
VIEW=rot((0,1,0),ANG[1])@rot((1,0,0),ANG[0])@rot((0,0,1),ANG[2])@trans(-POS)
PROJ=persp(35.0,640/430,20.0,20400*1.4)
def project(p):
    v=np.array([p[0],p[1],p[2],1.0])
    e=VIEW@v
    c=PROJ@e
    if c[3]<=0: return None, e
    ndc=c[:3]/c[3]
    # screen in 640x480 coords, viewport (0,25,640,430) y from top
    sx=(ndc[0]+1)/2*640
    sy=25+(1-(ndc[1]+1)/2)*430
    return (sx,sy,ndc[2]), e
