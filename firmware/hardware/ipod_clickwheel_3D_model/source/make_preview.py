#!/usr/bin/env python3
"""Render an exact-geometry overview of the prototype, not an AI image."""
from pathlib import Path
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Polygon, Rectangle
import numpy as np
from PIL import Image

from build_model import ROOT, P, build_caps, icon_polygons

BG = "#f5f6f7"
INK = "#19232d"
MUTED = "#596671"
ACCENT = "#187f85"
EDGE = "#87929e"


def triangles(shape):
    v, f = shape.tessellate(.025, .10)
    return np.array([[v[i].toTuple() for i in face] for face in f])


def mesh_colors(tri, base=(.84, .86, .89), engraving=False):
    normals = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
    light = np.array([-.3, -.4, .86]); light /= np.linalg.norm(light)
    brightness = .66 + .34*np.maximum(normals @ light, 0)
    colors = np.clip(brightness[:, None]*base, 0, 1)
    if engraving:
        floors = (abs(tri[:, :, 2]+P.engraving_depth).max(axis=1)<1e-5) & (normals[:, 2]>.9)
        colors[floors] = [.25, .28, .32]
    return colors


def render_solids(items, width=1200, height=650, elev=33, azim=-65):
    """Orthographic software z-buffer; avoids painter-order mesh artefacts."""
    tri=np.concatenate([triangles(shape) for shape,base,engrave in items])
    colors=np.concatenate([mesh_colors(triangles(shape),base,engrave) for shape,base,engrave in items])
    el,az=np.radians([elev,azim])
    view=np.array([np.cos(el)*np.cos(az),np.cos(el)*np.sin(az),np.sin(el)])
    right=np.array([-np.sin(az),np.cos(az),0.])
    up=np.cross(view,right)
    projected=tri @ np.array([right,up,view]).T
    lo=projected[:,:,:2].min(axis=(0,1));hi=projected[:,:,:2].max(axis=(0,1))
    scale=.90*min(width/(hi[0]-lo[0]),height/(hi[1]-lo[1]))
    projected[:,:,:2]=(projected[:,:,:2]-(lo+hi)/2)*scale
    projected[:,:,0]+=width/2
    projected[:,:,1]=height/2-projected[:,:,1]
    bg=np.array([245,246,247],dtype=np.uint8)
    pixels=np.broadcast_to(bg,(height,width,3)).copy()
    depth=np.full((height,width),-np.inf)
    for vertices,color in zip(projected,colors):
        minx=max(0,int(np.floor(vertices[:,0].min())))
        maxx=min(width-1,int(np.ceil(vertices[:,0].max())))
        miny=max(0,int(np.floor(vertices[:,1].min())))
        maxy=min(height-1,int(np.ceil(vertices[:,1].max())))
        if minx>maxx or miny>maxy: continue
        x0,y0,z0=vertices[0];x1,y1,z1=vertices[1];x2,y2,z2=vertices[2]
        denom=(y1-y2)*(x0-x2)+(x2-x1)*(y0-y2)
        if abs(denom)<1e-9: continue
        xx,yy=np.meshgrid(np.arange(minx,maxx+1)+.5,np.arange(miny,maxy+1)+.5)
        aa=((y1-y2)*(xx-x2)+(x2-x1)*(yy-y2))/denom
        bb=((y2-y0)*(xx-x2)+(x0-x2)*(yy-y2))/denom
        cc=1-aa-bb
        zz=aa*z0+bb*z1+cc*z2
        d=depth[miny:maxy+1,minx:maxx+1]
        mask=(aa>=-1e-8)&(bb>=-1e-8)&(cc>=-1e-8)&(zz>d)
        d[mask]=zz[mask]
        pixels[miny:maxy+1,minx:maxx+1][mask]=np.uint8(np.clip(color*255,0,255))
    return Image.fromarray(pixels)


def dim(ax, x1, y1, x2, y2, label, dx=0, dy=0):
    ax.annotate("", (x1,y1), (x2,y2), arrowprops=dict(arrowstyle="<->", lw=.85, color=ACCENT,
                                                   shrinkA=0, shrinkB=0))
    ax.text((x1+x2)/2+dx, (y1+y2)/2+dy, label, color=ACCENT, ha="center", va="center",
            fontsize=10, bbox=dict(facecolor=BG, edgecolor="none", pad=1.5))


def side_profile(ax):
    t=P.face_thickness; r=P.wheel_diameter/2; ri=P.opening_diameter/2; c=P.front_chamfer
    profile=np.array([[ri+c,0],[r-c,0],[r,-c],[r,-t],[r+.8,-t-.8],
                      [r+.8,-t-1.1],[r-1,-t-1.1],[r-1,-t],[ri,-t],[ri,-c]])
    for sign in [-1,1]:
        points=profile*[sign,1]
        ax.add_patch(Polygon(points, facecolor="#d9dfe5", edgecolor=EDGE, lw=.8))
        a=sign*ri; b=sign*(ri+P.select_guide_wall)
        ax.add_patch(Rectangle((min(a,b),-P.select_guide_depth),abs(b-a),P.select_guide_depth-t,
                              facecolor="#d9dfe5",edgecolor=EDGE,lw=.8))
    for x in [-P.button_radius,P.button_radius]:
        v=np.array([[-1.8,-t],[1.8,-t],[1.2,-t-.6],[1.2,-P.cap_depth],
                    [-1.2,-P.cap_depth],[-1.2,-t-.6]])+[x,0]
        ax.add_patch(Polygon(v, facecolor="#d9dfe5", edgecolor=EDGE, lw=.8))
    # SELECT: straight neck, flat flange and short contact tip.
    rs=P.select_diameter/2
    rf=rs+P.select_lip_overhang; f0=P.select_flange_start; f1=P.select_flange_end
    points=[[0,0],[rs-c,0],[rs,-c],[rs,-f0],[rf,-f0],[rf,-f1],
            [1.2,-f1],[1.2,-P.cap_depth],[-1.2,-P.cap_depth],[-1.2,-f1],
            [-rf,-f1],[-rf,-f0],[-rs,-f0],[-rs,-c],[-rs+c,0]]
    ax.add_patch(Polygon(points, facecolor="#b9c4ce", edgecolor=EDGE, lw=.8))
    pcbtop=-P.pcb_front_depth
    ax.add_patch(Rectangle((-22,-P.reference_stack),44,P.pcb_thickness,
                           facecolor="#b8d4c2",edgecolor="#729880",lw=.8))
    for x in [-P.button_radius,0,P.button_radius]:
        ax.add_patch(Rectangle((x-2,pcbtop),4,P.switch_height,facecolor="#424d56",edgecolor="none"))
    ax.text(-21.4,-P.reference_stack+P.pcb_thickness/2,"FR4",fontsize=7,color="#456a53",va="center")
    for y in [0,-P.reference_stack]: ax.plot([23.3,27],[y,y],color=ACCENT,lw=.6)
    dim(ax,26,0,26,-P.reference_stack,"5,8*",dx=0)
    ax.set_xlim(-25,29); ax.set_ylim(-6.8,1.7); ax.set_aspect("equal"); ax.axis("off")


def main():
    ring,select=build_caps()
    fig=plt.figure(figsize=(16,10),facecolor=BG)
    fig.text(.045,.955,"CLICK WHEEL",fontsize=25,weight="bold",color=INK)
    fig.text(.045,.917,"Печатные колпачки  ·  прототип v0.2  ·  все размеры в мм",fontsize=12,color=MUTED)
    fig.text(.955,.952,"44 / 15",fontsize=24,color=ACCENT,ha="right",weight="bold")
    fig.text(.955,.920,"Две отдельные подвижные детали",fontsize=10,color=MUTED,ha="right")

    fig.text(.055,.867,"01   ВИД СПЕРЕДИ",fontsize=10,color=MUTED,weight="bold")
    ax=fig.add_axes([.045,.365,.435,.48]); ax.set_facecolor(BG)
    ax.add_patch(Circle((0,0),22.8,fill=False,ec="#b7c2ca",lw=.8,ls=(0,(3,3))))
    ax.add_patch(Circle((0,0),22,fc="#e2e6eb",ec=EDGE,lw=1))
    ax.add_patch(Circle((0,0),21.85,fill=False,ec="#f9fafb",lw=1))
    ax.add_patch(Circle((0,0),P.opening_diameter/2,fc="#48545e",ec=EDGE,lw=.65))
    ax.add_patch(Circle((0,0),7.5,fc="#c8d2db",ec=EDGE,lw=.65))
    ax.add_patch(Circle((0,0),7.35,fill=False,ec="#eef1f4",lw=.8))
    for poly in icon_polygons()[0]: ax.add_patch(Polygon(poly,color="#424d57",lw=0))
    ax.plot([-22,-22],[22,26],color=ACCENT,lw=.7);ax.plot([22,22],[22,26],color=ACCENT,lw=.7)
    dim(ax,-22,25,22,25,"Ø44,0",dy=.15)
    dim(ax,-7.5,-1,7.5,-1,"Ø15,0",dy=0)
    ax.annotate("Зазор 0,20\nс каждой стороны",xy=(7.6,-.8),xytext=(11,-12),fontsize=9,color=ACCENT,
                arrowprops=dict(arrowstyle="-",color=ACCENT,lw=.7),ha="left")
    ax.set_xlim(-27,28);ax.set_ylim(-25,29);ax.set_aspect("equal");ax.axis("off")

    fig.text(.525,.867,"02   СОБРАННЫЕ КОЛПАЧКИ",fontsize=10,color=MUTED,weight="bold")
    ax=fig.add_axes([.515,.55,.44,.29])
    ax.imshow(render_solids([(ring,(.84,.86,.89),True),(select,(.74,.79,.84),False)]))
    ax.axis("off")

    fig.text(.525,.505,"03   ИЗНАНКА / SELECT ОТДЕЛЬНО",fontsize=10,color=MUTED,weight="bold")
    ax=fig.add_axes([.515,.28,.44,.20])
    back_ring=ring.rotate((0,0,0),(1,0,0),180).translate((-8,0,0))
    back_select=select.rotate((0,0,0),(1,0,0),180).translate((27,0,0))
    ax.imshow(render_solids([(back_ring,(.84,.86,.89),False),(back_select,(.74,.79,.84),False)],
                           width=1400,height=600,elev=33,azim=-73))
    ax.axis("off")
    fig.text(.53,.235,"4 боковых толкателя + 1 центральный  ·  контакт Ø2,4",fontsize=10,color=MUTED)

    fig.text(.055,.315,"04   РАЗРЕЗ / РАСЧЁТНАЯ ПОСАДКА",fontsize=10,color=MUTED,weight="bold")
    ax=fig.add_axes([.045,.175,.435,.13]);side_profile(ax)
    fig.text(.055,.157,"* 3,4 колпачок + 0,8 кнопка + 1,6 FR4 = 5,8",fontsize=9,color=MUTED)
    fig.text(.055,.130,"Радиус боковых кнопок: 16,0 — принят предварительно.",fontsize=10,color=INK)

    fig.add_artist(plt.Line2D([.045,.955],[.108,.108],transform=fig.transFigure,color="#d0d7dd",lw=1))
    fig.text(.045,.078,"СКРЫТЫЕ БУРТИКИ",fontsize=10,color=ACCENT,weight="bold")
    fig.text(.045,.052,"Кольцо Ø45,6  ·  SELECT Ø20,0  ·  лицевая стенка 1,0",fontsize=10,color=INK)
    fig.text(.56,.078,"STL проверены на замкнутость; детали не пересекаются.",fontsize=10,color=INK)
    fig.text(.56,.052,"Посадка в корпус и реальный ход требуют примерки.",fontsize=10,color=MUTED)
    fig.savefig(ROOT / "preview.png",dpi=150,facecolor=BG)
    draw_select_detail()
    print(ROOT / "preview.png")


def draw_select_detail():
    fig=plt.figure(figsize=(12,6.5),facecolor=BG)
    fig.text(.06,.925,"SELECT / v0.2",fontsize=23,weight="bold",color=INK)
    fig.text(.06,.875,"Разрез через центр: широкий буртик под направляющей",fontsize=12,color=MUTED)
    ax=fig.add_axes([.045,.225,.91,.60]);ax.set_facecolor(BG)
    rs=P.select_diameter/2;ri=P.opening_diameter/2
    rf=rs+P.select_lip_overhang;gw=P.select_guide_wall
    gd=P.select_guide_depth; f0=P.select_flange_start;f1=P.select_flange_end
    for sign in [-1,1]:
        ring=np.array([[ri,0],[14,0],[14,-1],[ri+gw,-1],[ri+gw,-gd],[ri,-gd]])*[sign,1]
        ax.add_patch(Polygon(ring,facecolor="#cdd5dd",edgecolor="#768593",lw=1))
    outline=[[-rs,0],[rs,0],[rs,-f0],[rf,-f0],[rf,-f1],[1.2,-f1],
             [1.2,-P.cap_depth],[-1.2,-P.cap_depth],[-1.2,-f1],[-rf,-f1],[-rf,-f0],[-rs,-f0]]
    ax.add_patch(Polygon(outline,facecolor="#a8cbdc",edgecolor="#547f96",lw=1))
    ax.add_patch(Rectangle((-14,-P.reference_stack),28,P.pcb_thickness,
                          facecolor="#c3dbc9",edgecolor="#779781",lw=.8))
    ax.add_patch(Rectangle((-2,-P.pcb_front_depth),4,P.switch_height,
                          facecolor="#43535f",edgecolor="none"))
    ax.text(10.4,-.55,"КОЛЬЦО",fontsize=8,color=MUTED,ha="center")
    ax.text(0,-1.0,"SELECT",fontsize=12,color="#355c72",ha="center",va="center",weight="bold")
    ax.text(-12.7,-5.0,"FR4",fontsize=9,color="#557b5e",va="center")
    dim(ax,-rs,1.0,rs,1.0,"Ø15",dy=0)
    dim(ax,-rf,-2.73,rf,-2.73,"Буртик Ø20 × 0,8",dy=0)
    ax.annotate("По 0,20 мм\nна сторону",xy=(7.6,-.4),xytext=(10.7,1.8),
                fontsize=10,color=ACCENT,ha="left",va="center",
                arrowprops=dict(arrowstyle="-",color=ACCENT,lw=.9))
    ax.annotate("Направляющая\nдлиной 2,0 мм",xy=(-8.3,-1.5),xytext=(-16.7,-2.1),
                fontsize=10,color=ACCENT,ha="left",va="center",
                arrowprops=dict(arrowstyle="-",color=ACCENT,lw=.9))
    ax.annotate("Зазор по оси\n0,30 мм",xy=(8.4,-2.15),xytext=(11.5,-3.5),
                fontsize=10,color=ACCENT,ha="left",va="center",
                arrowprops=dict(arrowstyle="-",color=ACCENT,lw=.9))
    ax.set_xlim(-18,18);ax.set_ylim(-6.2,2.8);ax.set_aspect("equal");ax.axis("off")
    fig.text(.06,.175,"Перекрытие буртика с отверстием: 0,5 → 2,3 мм на сторону",fontsize=12,color=INK,weight="bold")
    fig.text(.06,.12,"SELECT вставляется с изнанки кольца; снизу устанавливается плата с кнопкой.",fontsize=10.5,color=MUTED)
    fig.text(.06,.075,"Для печати SELECT нужны поддержки под буртиком. Посадку проверить пробной полоской.",fontsize=10.5,color=MUTED)
    fig.savefig(ROOT / "reference/select_detail.png",dpi=160,facecolor=BG)
    plt.close(fig)


if __name__=="__main__": main()
