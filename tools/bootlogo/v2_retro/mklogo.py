from PIL import Image, ImageDraw, ImageFont
import math, os
W,H=240,144
img=Image.new('RGB',(W,H)); d=ImageDraw.Draw(img)
HOR=92
# sky: banded gradient (no smooth gradient -> retro look)
sky=[(10,6,30),(20,10,52),(38,14,78),(64,18,98),(98,24,110),(140,34,112),(186,52,104)]
bh=HOR/len(sky)
for i,c in enumerate(sky): d.rectangle([0,int(i*bh),W,int((i+1)*bh)],fill=c)
# stars
import random; random.seed(7)
for _ in range(45):
    x,y=random.randrange(W),random.randrange(int(HOR*0.55))
    d.point((x,y),fill=random.choice([(255,255,255),(180,200,255),(255,220,160)]))
# sun with horizontal cut stripes
sx,sy,sr=120,HOR-2,30
sun=[(255,236,90),(255,200,70),(255,160,70),(255,110,90),(240,70,120)]
for y in range(sy-sr,sy+1):
    w=int(math.sqrt(max(0,sr*sr-(y-sy)**2)))
    t=(y-(sy-sr))/sr
    c=sun[min(4,int(t*5))]
    cut = t>0.45 and ((y-(sy-sr))%6) < (1+int((t-0.45)*6))
    if not cut: d.line([sx-w,y,sx+w,y],fill=c)
# floor grid
d.rectangle([0,HOR,W,H],fill=(14,4,30))
for k in range(1,7):
    y=HOR+int((k/6)**2*(H-HOR))
    d.line([0,y,W,y],fill=(255,60,200) if k>1 else (255,120,230))
for k in range(-5,6):
    x0=sx+k*14; x1=sx+k*60
    d.line([x0,HOR,x1,H],fill=(200,40,180))
# title LINUX, chunky bitmap font scaled x3 with shadow
f=ImageFont.load_default()
def btext(txt,scale,col,shadow,cx,y):
    m=Image.new('1',(200,20)); dd=ImageDraw.Draw(m); dd.text((0,0),txt,font=f,fill=1); dd.text((1,0),txt,font=f,fill=1)
    bb=m.getbbox(); m=m.crop(bb)
    m=m.resize((m.width*scale,m.height*scale),Image.NEAREST)
    x=cx-m.width//2
    for dx,dy,c in ((2,2,shadow),(0,0,col)):
        img.paste(c,(x+dx,y+dy),m)
    return m.height
GLY={'L':["10000","10000","10000","10000","10000","10000","11111"],
     'I':["111","010","010","010","010","010","111"],
     'N':["10001","11001","10101","10011","10001","10001","10001"],
     'U':["10001","10001","10001","10001","10001","10001","01110"],
     'X':["10001","10001","01010","00100","01010","10001","10001"]}
def ptitle(txt,sc,y,col,col2,shadow):
    widths=[len(GLY[c][0]) for c in txt]; tot=sum(widths)*sc+(len(txt)-1)*sc
    x=W//2-tot//2
    for c,wd in zip(txt,widths):
        for r,row in enumerate(GLY[c]):
            for k,b in enumerate(row):
                if b=='1':
                    X=x+k*sc; Y=y+r*sc
                    d.rectangle([X+2,Y+2,X+sc+1,Y+sc+1],fill=shadow)
        for r,row in enumerate(GLY[c]):
            for k,b in enumerate(row):
                if b=='1':
                    X=x+k*sc; Y=y+r*sc
                    d.rectangle([X,Y,X+sc-1,Y+sc-1],fill=col if r<4 else col2)
        x+=wd*sc+sc
ptitle('LINUX',5,6,(130,245,255),(60,170,255),(70,0,100))
# decoder (set-top box)
bx,by,bw,bh2=62,104,116,24
d.rectangle([bx,by,bx+bw,by+bh2],fill=(40,42,52),outline=(10,10,14))
d.rectangle([bx+1,by+1,bx+bw-1,by+3],fill=(70,72,86))           # top highlight
d.rectangle([bx+3,by+bh2,bx+10,by+bh2+2],fill=(10,10,14)); d.rectangle([bx+bw-10,by+bh2,bx+bw-3,by+bh2+2],fill=(10,10,14))
# 7-seg display "HELO"
dx0,dy0=bx+38,by+7
d.rectangle([dx0-3,dy0-2,dx0+36,dy0+12],fill=(18,6,6),outline=(8,8,10))
SEG={'H':'bcefg','E':'adefg','L':'def','O':'abcdef'}
def digit(x,y,ch,on=(255,50,40),off=(50,14,12)):
    segs={'a':[(x+1,y),(x+5,y)],'b':[(x+6,y+1),(x+6,y+4)],'c':[(x+6,y+6),(x+6,y+9)],
          'd':[(x+1,y+10),(x+5,y+10)],'e':[(x,y+6),(x,y+9)],'f':[(x,y+1),(x,y+4)],'g':[(x+1,y+5),(x+5,y+5)]}
    for s,ln in segs.items(): d.line(ln,fill=on if s in SEG[ch] else off)
for i,ch in enumerate('HELO'): digit(dx0+i*9,dy0,ch)
# buttons POWER CH+ CH-, LED, IR window
for i in range(3): d.rectangle([bx+82+i*9,by+10,bx+87+i*9,by+14],fill=(90,92,104),outline=(20,20,24))
d.rectangle([bx+6,by+9,bx+30,by+16],fill=(25,10,20))          # IR window
d.point((bx+108,by+18),fill=(60,255,90)); d.point((bx+109,by+18),fill=(60,255,90))
d.text((bx+7,by+17),'',fill=0)
# pixel Tux from the SVG, quantized
tux=Image.open('tux_small.png').convert('RGBA')
a=tux.split()[3].point(lambda v:255 if v>128 else 0)
PAL=[(12,12,16),(70,70,80),(250,250,250),(190,190,200),(250,200,30),(220,140,20)]
q=Image.new('RGB',tux.size); tp=tux.load(); qp=q.load()
for yy in range(tux.height):
    for xx in range(tux.width):
        r,g,b,_=tp[xx,yy]
        qp[xx,yy]=min(PAL,key=lambda c:(c[0]-r)**2+(c[1]-g)**2+(c[2]-b)**2)
img.paste(q,(bx+bw//2-tux.width//2+22,by-tux.height+3),a)

# ---- MIPS theme: CPU chip (left) and asm terminal (right) ----
F35={'A':"010101111101101",'B':"110101110101110",'C':"011100100100011",'D':"110101101101110",
 'E':"111100110100111",'I':"111010010010111",'J':"001001001101010",'K':"101101110101101",
 'L':"100100100100111",'M':"101111111101101",'N':"110101101101101",'O':"010101101101010",
 'P':"110101110100100",'R':"110101110101101",'S':"011100010001110",'T':"111010010010010",
 'U':"101101101101111",'W':"101101111111101",'0':"111101101101111",'2':"110001010100111",
 '3':"110001010001110",'4':"101101111001001",'8':"111101111101111",'F':"111100110100100",
 'c':"000011100100011",'$':"011110010011110",',':"000000000010100",'x':"000101010101000",
 ' ':"000000000000000",'_':"000000000000111",'1':"010110010010111",'6':"011100111101111"}
def t35(txt,x,y,col):
    for ch in txt:
        g=F35[ch]
        for i,b in enumerate(g):
            if b=='1': d.point((x+i%3,y+i//3),fill=col)
        x+=4
# chip
cx0,cy0,cs=16,59,28
for i in range(4,cs-2,4):
    for (px_,py_,w_,h_) in ((cx0+i,cy0-3,1,2),(cx0+i,cy0+cs+1,1,2),(cx0-3,cy0+i,2,1),(cx0+cs+1,cy0+i,2,1)):
        d.rectangle([px_,py_,px_+w_-1,py_+h_-1],fill=(200,200,215))
d.rectangle([cx0,cy0,cx0+cs,cy0+cs],fill=(34,34,44),outline=(12,12,16))
d.rectangle([cx0+1,cy0+1,cx0+cs-1,cy0+1],fill=(70,70,86))
d.point((cx0+3,cy0+3),fill=(120,120,140))
t35('MIPS',cx0+7,cy0+8,(255,200,60))
t35('34Kc',cx0+7,cy0+16,(130,245,255))
# terminal
tx0,ty0=172,57
d.rectangle([tx0,ty0,tx0+56,ty0+31],fill=(4,14,8),outline=(40,200,90))
d.rectangle([tx0+1,ty0+1,tx0+55,ty0+1],fill=(20,60,30))
code=['LUI $T0,BF20','ADDIU $SP,8','JR $RA','NOP']
for i,l in enumerate(code): t35(l,tx0+4,ty0+4+i*7,(90,255,120))
d.rectangle([tx0+20,ty0+25,tx0+22,ty0+29],fill=(90,255,120))
# subtitle
f2=ImageFont.load_default()
def small(txt,y,col):
    m=Image.new('1',(240,14)); ImageDraw.Draw(m).text((0,0),txt,font=f2,fill=1); m=m.crop(m.getbbox())
    img.paste((0,0,0),(W//2-m.width//2+1,y+1),m); img.paste(col,(W//2-m.width//2,y),m)
small('MSTAR MSD7818 * 552 MHz',47,(255,220,120))
img.save('retro_240.png')
big=img.resize((W*4,H*4),Image.NEAREST); big.save('retro_preview.png')
jpg=img.resize((720,576),Image.NEAREST)
for Q in (92,90,88,85,82,80,78,76,75,72,70,68,65):
    jpg.save('linux_logo.jpg',quality=Q,subsampling=0)
    if os.path.getsize('linux_logo.jpg')<0xF000: break
print('Q',Q,os.path.getsize('linux_logo.jpg'))
