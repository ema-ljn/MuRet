import numpy as np, math
def angle_matrix_rad(a):
    # Half-Life AngleMatrix with radians: a=(roll x, pitch y, yaw z); R = Rz*Ry*Rx
    sr,cr=math.sin(a[0]),math.cos(a[0]); sp,cp=math.sin(a[1]),math.cos(a[1]); sy,cy=math.sin(a[2]),math.cos(a[2])
    m=np.zeros((3,3))
    m[0][0]=cp*cy; m[1][0]=cp*sy; m[2][0]=-sp
    m[0][1]=sr*sp*cy+cr*-sy; m[1][1]=sr*sp*sy+cr*cy; m[2][1]=sr*cp
    m[0][2]=cr*sp*cy+-sr*-sy; m[1][2]=cr*sp*sy+-sr*cy; m[2][2]=cr*cp
    return m
def angle_matrix_deg(a): return angle_matrix_rad([math.radians(x) for x in a])
def bone_mats(bmd, action=0, frame=0):
    mats=[]
    for b in bmd['bones']:
        if b is None: mats.append(None); continue
        pos,rot=b['keys'][action]
        k=min(frame,len(pos)-1)
        M=np.eye(4); M[:3,:3]=angle_matrix_rad(rot[k]); M[:3,3]=pos[k]
        if b['parent']>=0 and mats[b['parent']] is not None: M=mats[b['parent']]@M
        mats.append(M)
    return mats
def world_verts(bmd, pos, ang, scale, action=0, frame=0):
    mats=bone_mats(bmd,action,frame)
    R=angle_matrix_deg(ang)
    out=[]
    for mi,me in enumerate(bmd['meshes']):
        vs=[]
        for (b,p) in me['verts']:
            M=mats[b] if 0<=b<len(mats) and mats[b] is not None else np.eye(4)
            v=M[:3,:3]@np.array(p)+M[:3,3]
            vs.append(R@(v*scale)+np.array(pos))
        out.append(np.array(vs))
    return out
