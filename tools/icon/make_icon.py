# AmF X1 app icon: a rounded tile in deep ink with a soft violet-to-amber glow, three fader channels whose caps form a rising line,
# and an amber cap on the top channel. Drawn at 1024 px, saved as PNG 256/512 and a multi-size ICO.
import sys
from PIL import Image, ImageDraw, ImageFilter
N=1024
def lerp(a,b,t): return tuple(int(a[i]+(b[i]-a[i])*t) for i in range(len(a)))
base=Image.new('RGBA',(N,N),(0,0,0,0))
# tile background gradient
bg=Image.new('RGBA',(N,N)); px=bg.load()
top=(26,20,46); bot=(8,10,18)
for y in range(N):
    c=lerp(top,bot,y/N)+(255,)
    for x in range(N): px[x,y]=c
glow=Image.new('RGBA',(N,N),(0,0,0,0)); g=ImageDraw.Draw(glow)
g.ellipse((-200,-260,820,700),fill=(124,58,237,150)); g.ellipse((380,420,1250,1250),fill=(234,88,12,150))
glow=glow.filter(ImageFilter.GaussianBlur(170)); bg=Image.alpha_composite(bg,glow)
mask=Image.new('L',(N,N),0); ImageDraw.Draw(mask).rounded_rectangle((40,40,N-40,N-40),radius=230,fill=255)
base.paste(bg,(0,0),mask)
d=ImageDraw.Draw(base)
# inner hairline
d.rounded_rectangle((40,40,N-40,N-40),radius=230,outline=(255,255,255,40),width=6)
# three fader slots
xs=[332,512,692]; caps=[640,500,340]
ov=Image.new('RGBA',(N,N),(0,0,0,0)); o=ImageDraw.Draw(ov)
for i,x in enumerate(xs):
    o.rounded_rectangle((x-16,250,x+16,800),radius=16,fill=(255,255,255,46))
for i,x in enumerate(xs):
    col=[(167,139,250),(244,114,182),(251,146,60)][i]
    o.rounded_rectangle((x-16,caps[i],x+16,800),radius=16,fill=col+(235,))
base=Image.alpha_composite(base,ov); d=ImageDraw.Draw(base)
# the rising line through the caps
line=Image.new('RGBA',(N,N),(0,0,0,0)); l=ImageDraw.Draw(line)
pts=[(x,c) for x,c in zip(xs,caps)]
l.line(pts,fill=(255,255,255,90),width=14,joint='curve')
base=Image.alpha_composite(base,line); d=ImageDraw.Draw(base)
for i,(x,c) in enumerate(pts):
    sh=Image.new('RGBA',(N,N),(0,0,0,0)); ImageDraw.Draw(sh).rounded_rectangle((x-78,c-40+14,x+78,c+40+14),radius=22,fill=(0,0,0,140))
    base=Image.alpha_composite(base,sh.filter(ImageFilter.GaussianBlur(14))); d=ImageDraw.Draw(base)
    fill=(251,191,36,255) if i==2 else (241,245,249,255)
    d.rounded_rectangle((x-78,c-40,x+78,c+40),radius=22,fill=fill)
    d.line((x-56,c,x+56,c),fill=(15,20,30,200),width=8)
out=sys.argv[1]
base.resize((512,512),Image.LANCZOS).save(out+'/amfx1-512.png')
base.resize((256,256),Image.LANCZOS).save(out+'/amfx1.png')
base.resize((256,256),Image.LANCZOS).save(out+'/amfx1.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print('ok')
