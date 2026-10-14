from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math
W,H=1024,576            # designed in 16:9, squashed to 720x576 for MBoot
img=Image.new('RGB',(W,H))
px=img.load()
cx,cy=300,260
for y in range(H):
    for x in range(W):
        d=math.hypot((x-cx)/W,(y-cy)/H)
        t=max(0.0,1-d*1.6)
        px[x,y]=(int(8+22*t), int(12+40*t), int(24+70*t))
dr=ImageDraw.Draw(img,'RGBA')
# faint tile grid, a nod to the old WIWA squares
for gx in range(0,W,64):
    for gy in range(0,H,64):
        a=int(10+14*max(0,1-math.hypot(gx-760,gy-300)/600))
        dr.rounded_rectangle([gx+6,gy+6,gx+58,gy+58],radius=10,outline=(80,200,255,a),width=2)
# glow behind Tux
glow=Image.new('L',(W,H),0); ImageDraw.Draw(glow).ellipse([110,80,490,460],fill=110)
glow=glow.filter(ImageFilter.GaussianBlur(60))
img=Image.composite(Image.new('RGB',(W,H),(60,170,255)),img,glow)
dr=ImageDraw.Draw(img,'RGBA')
tux=Image.open('tux.png').convert('RGBA')
img.paste(tux,(300-tux.width//2,95),tux)
bold=ImageFont.truetype('/usr/share/fonts/TTF/DejaVuSans-Bold.ttf',124)
reg=ImageFont.truetype('/usr/share/fonts/TTF/DejaVuSans.ttf',34)
mono=ImageFont.truetype('/usr/share/fonts/adobe-source-code-pro/SourceCodePro-Medium.otf',20)
tx=520
dr.text((tx,165),'Linux',font=bold,fill=(245,248,255))
dr.rectangle([tx+6,328,tx+400,332],fill=(255,190,40))
dr.text((tx+6,345),'on MStar MSD7818',font=reg,fill=(120,205,255))
dr.text((tx+6,398),'mips32r2 · 34Kc · 6.7 · buildroot',font=mono,fill=(150,170,200))
dr.text((W//2,H-40),'WIWA HD95',font=ImageFont.truetype('/usr/share/fonts/TTF/DejaVuSans.ttf',20),fill=(90,110,140),anchor='mm')
img.save('design_16x9.png')
small=img.resize((720,576),Image.LANCZOS)
import os
for Q in (88,85,82,80,78,75):
    small.save('linux_logo.jpg',quality=Q,subsampling=0,optimize=False,progressive=False)
    if os.path.getsize('linux_logo.jpg')<0xF000: break
print('Q',Q)
#small.save('linux_logo.jpg',quality=Q,subsampling=0,optimize=False,progressive=False)
img.resize((1920,1080),Image.LANCZOS).save('preview_1080.png')
import os; print(os.path.getsize('linux_logo.jpg'))
