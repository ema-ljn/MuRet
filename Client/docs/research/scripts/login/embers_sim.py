import os
# Simulacion en Python (no emulacion del main.exe) de las brasas del login, segun 0x42b1e0/0x5303a0.
import random, struct, statistics
raw = open(os.path.join(os.environ.get('MURET_CLIENT_DATA', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'assets', 'Data')), 'world56', 'TerrainHeight.OZB'),'rb').read()
H = [b*3.0 for b in raw[0x43c:0x43c+65536]]
def height(x, y):
    if x < 0 or y < 0: return 0.0
    fx, fy = x*0.01, y*0.01
    xi, yi = int(fx), int(fy)
    if yi*256+xi > 0xffff: return None
    xi &= 255; yi &= 255
    dx, dy = fx-int(fx), fy-int(fy)
    i1 = yi*256+xi; i2 = ((yi+1)&255)*256+xi; i3 = yi*256+((xi+1)&255); i4 = ((yi+1)&255)*256+((xi+1)&255)
    a = (H[i2]-H[i1])*dy + H[i1]
    b = (H[i4]-H[i3])*dy + H[i3]
    return dx*(b-a)+a
HERO = (11679.0, 14191.0, 333.0); CAMY = 12678.0
def rnd(n): return random.randrange(32768) % n
def spawn():
    x = (rnd(1600)-800) + HERO[0]; y = (rnd(1400)-500) + HERO[1]; z = (rnd(1000)+50) + HERO[2]
    vx = (rnd(64)+64)*-0.1
    if y < CAMY+400: vx = 3.2 - vx
    vy = (rnd(32)-16)*0.1; vz = (rnd(32)-16)*0.1
    return [x,y,z,vx,vy,vz,1.0]
random.seed(1)
flights=[]; land=[]
for k in range(4000):
    e = spawn(); n=0
    while True:
        n+=1
        h = height(e[0],e[1]) or 0.0
        if h < e[2]:
            for i in range(3):
                e[3+i] += (rnd(16)-8)*0.1
            e[0]+=e[3]; e[1]+=e[4]; e[2]+=e[5]
        else:
            break
        if n>5000: break
    flights.append(n); land.append((e[0],e[1],e[2]))
flights.sort()
print('pasos en el aire: mediana', statistics.median(flights), 'p10', flights[len(flights)//10], 'p90', flights[9*len(flights)//10], 'max', flights[-1])
print('aterrizaje x medio', statistics.mean(l[0] for l in land), 'y', statistics.mean(l[1] for l in land), 'z', statistics.mean(l[2] for l in land))
print('altura del terreno en el heroe', height(HERO[0],HERO[1]), 'en el personaje', height(10603,14112))
