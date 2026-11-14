#!/usr/bin/env python3
"""MonoPod Studio. An offline theme editor + LAN transfer tool (Tkinter + Pillow)."""
import json
import tempfile
import threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog
from tkinter.scrolledtext import ScrolledText
from PIL import ImageTk
from theme_format import ThemeOptions, encode_theme, demo_preview, verify_theme, find_default_font
from device_api import DeviceClient
from usb_sync import UsbClient, get_ports

BG='#151a1e'
PANEL='#20272d'
TEXT='#e9edf1'
MUTED='#a7b3bc'
ACCENT='#75b7a0'
APP='MonoPod Studio 0.9.1.1'

class Studio(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP)
        self.geometry('1110x735')
        self.minsize(965,660)
        self.configure(bg=BG)
        self.font_path=tk.StringVar(value=find_default_font())
        self.device=tk.StringVar(value='http://192.168.4.1')
        self.usb_port=tk.StringVar(value='')
        self.main_size=tk.IntVar(value=15)
        self.small_size=tk.IntVar(value=11)
        self.smoothing=tk.BooleanVar(value=False)
        self.palette={k:tk.StringVar(value=v) for k,v in {
            'bg':'#ced6c9','fg':'#202b21','selected':'#263c29','muted':'#8b9c86'
        }.items()}
        self.preview_photo=None
        self._preview_job=None
        self.status_text=tk.StringVar(value='Start Wi-Fi Sync on player, then use http://<IP shown on its screen> (or AP fallback)')
        self._style()
        self._build()
        for v in [self.font_path,self.main_size,self.small_size,self.smoothing,*self.palette.values()]:
            v.trace_add('write',self.request_preview)
        self.request_preview()

    def _style(self):
        style=ttk.Style(self)
        style.theme_use('clam')
        style.configure('Main.TNotebook',background=BG,borderwidth=0)
        style.configure('Main.TNotebook.Tab',background=PANEL,foreground=TEXT,padding=(18,10),font=('Arial',10,'bold'))
        style.map('Main.TNotebook.Tab',background=[('selected','#314049')],foreground=[('selected','#ffffff')])
        style.configure('TProgressbar',troughcolor=BG,background=ACCENT,borderwidth=0)

    def label(self,parent,text="",**kwargs):
        return tk.Label(parent,text=text,bg=parent.cget('bg'),fg=kwargs.pop('fg',TEXT),**kwargs)

    def btn(self,parent,text,command,primary=False):
        return tk.Button(parent,text=text,command=command,bg=ACCENT if primary else '#34434c',
                         fg='#10251d' if primary else '#f0f5f8',activebackground='#90c8b5',
                         activeforeground='#0c1818',bd=0,padx=13,pady=9,
                         font=('Arial',10,'bold'),cursor='hand2')

    def entry(self,parent,var,width=12):
        return tk.Entry(parent,textvariable=var,width=width,bg='#10171b',fg=TEXT,insertbackground=TEXT,
                        relief='flat',highlightthickness=1,highlightbackground='#4a5860',font=('Arial',11))

    def panel(self,parent):
        return tk.Frame(parent,bg=PANEL,padx=18,pady=14)

    def _build(self):
        head=tk.Frame(self,bg=BG,padx=22,pady=16);head.pack(fill='x')
        self.label(head,'MonoPod',font=('Arial',22,'bold')).pack(side='left')
        self.label(head,' / Studio',font=('Arial',17),fg=MUTED).pack(side='left',padx=(2,0))
        self.label(head,'160 × 128   •   ESP32 / ESP32-S3',font=('Arial',10),fg=MUTED).pack(side='right')
        tabs=ttk.Notebook(self,style='Main.TNotebook');tabs.pack(fill='both',expand=True,padx=20,pady=(0,8))
        editor=tk.Frame(tabs,bg=BG);device_tab=tk.Frame(tabs,bg=BG)
        tabs.add(editor,text='Theme Editor');tabs.add(device_tab,text='Device & Music')
        controls=self.panel(editor);controls.pack(side='left',fill='y',padx=(0,10),pady=10)
        controls.configure(width=390);controls.pack_propagate(False)
        self.label(controls,'TYPOGRAPHY',fg=ACCENT,font=('Arial',10,'bold')).pack(anchor='w',pady=(0,10))
        self.label(controls,'Local TTF / OTF font',font=('Arial',10)).pack(anchor='w')
        row=tk.Frame(controls,bg=PANEL);row.pack(fill='x',pady=(5,12))
        self.entry(row,self.font_path,width=31).pack(side='left',fill='x',expand=True)
        self.btn(row,'Browse',self.pick_font).pack(side='left',padx=(8,0))
        for title,var,low,high in [('Menu font size',self.main_size,8,20),('Small labels',self.small_size,8,16)]:
            row=tk.Frame(controls,bg=PANEL);row.pack(fill='x',pady=(0,10))
            self.label(row,title).pack(side='left')
            tk.Spinbox(row,from_=low,to=high,textvariable=var,width=5,bg='#10171b',fg=TEXT,
                       buttonbackground='#34434c',relief='flat').pack(side='right')
        check=tk.Checkbutton(controls,text='Font smoothing (default: Off)',variable=self.smoothing,
                             bg=PANEL,fg=TEXT,activebackground=PANEL,selectcolor='#10171b',activeforeground=TEXT)
        check.pack(anchor='w',pady=(0,12))
        self.label(controls,'PALETTE',fg=ACCENT,font=('Arial',10,'bold')).pack(anchor='w',pady=(6,10))
        for name,label in [('bg','Background'),('fg','Text'),('selected','Selected row'),('muted','Muted elements')]:
            row=tk.Frame(controls,bg=PANEL);row.pack(fill='x',pady=4)
            self.label(row,label).pack(side='left')
            self.entry(row,self.palette[name],width=11).pack(side='right')
        self.label(controls,'LOCAL THEME',fg=ACCENT,font=('Arial',10,'bold')).pack(anchor='w',pady=(12,6))
        for text,command,primary in [
            ('Export theme .mpt',self.export_theme,True),
            ('Save preview .png',self.save_preview,False),
            ('Save editor configuration',self.save_profile,False),
            ('Load editor configuration',self.load_profile,False),
        ]:
            self.btn(controls,text,command,primary).pack(fill='x',pady=2)
        self.label(controls,'Fonts are converted locally. No font files are\nbundled or sent to an external server.',
                   fg=MUTED,font=('Arial',9),justify='left').pack(anchor='w',side='bottom')

        right=self.panel(editor);right.pack(side='left',fill='both',expand=True,pady=10)
        self.label(right,'LIVE DISPLAY PREVIEW',fg=ACCENT,font=('Arial',10,'bold')).pack(anchor='w',pady=(0,12))
        screen=tk.Frame(right,bg='#050809',padx=18,pady=18);screen.pack(anchor='center',pady=10)
        self.preview_label=tk.Label(screen,bg='#050809',borderwidth=0)
        self.preview_label.pack()
        self.label(right,'Exact 160×128 composition · 3× nearest-neighbor zoom',fg=MUTED,font=('Arial',10)).pack(pady=14)
        self.label(right,'OFF — binary pixels. ON — 4-bit glyph coverage.\n'
                   'Theme is rasterized for 16 px and 12 px text lines.',fg=MUTED,justify='center').pack()
        self.preview_error=self.label(right,'',fg='#edaaaa',font=('Arial',9));self.preview_error.pack(pady=12)

        top=self.panel(device_tab);top.pack(fill='x',padx=10,pady=10)
        self.label(top,'LOCAL DEVICE',fg=ACCENT,font=('Arial',10,'bold')).grid(row=0,column=0,sticky='w',pady=(0,12))
        self.label(top,'Sync URL (from player):').grid(row=1,column=0,sticky='w')
        self.entry(top,self.device,34).grid(row=1,column=1,sticky='we',padx=(10,10))
        self.btn(top,'Connect / Refresh',self.connect_device,True).grid(row=1,column=2)
        top.columnconfigure(1,weight=1)
        self.device_info=self.label(top,'Not connected',fg=MUTED,font=('Arial',10),justify='left')
        self.device_info.grid(row=2,column=0,columnspan=3,sticky='w',pady=(15,0))
        self.label(top,'USB CDC port (ESP32-S3):').grid(row=3,column=0,sticky='w',pady=(12,0))
        self.entry(top,self.usb_port,34).grid(row=3,column=1,sticky='we',padx=(10,10),pady=(12,0))
        self.btn(top,'Detect USB ports',self.detect_usb).grid(row=3,column=2,pady=(12,0))
        self.btn(top,'USB Connect / Refresh',self.connect_usb).grid(row=4,column=2,pady=(8,0))
        mid=self.panel(device_tab);mid.pack(fill='x',padx=10,pady=10)
        self.label(mid,'THEME SYNC',fg=ACCENT,font=('Arial',10,'bold')).pack(anchor='w',pady=(0,10))
        row=tk.Frame(mid,bg=PANEL);row.pack(fill='x')
        self.btn(row,'Generate & Upload current theme',self.push_theme,True).pack(side='left',padx=(0,10))
        self.btn(row,'Restore built-in theme',self.reset_device_theme).pack(side='left',padx=(0,10))
        self.btn(row,'Apply smoothing setting',self.push_smoothing).pack(side='left')
        music=self.panel(device_tab);music.pack(fill='both',expand=True,padx=10,pady=10)
        self.label(music,'MUSIC TRANSFER',fg=ACCENT,font=('Arial',10,'bold')).pack(anchor='w',pady=(0,10))
        row=tk.Frame(music,bg=PANEL);row.pack(fill='x')
        self.btn(row,'Upload over Wi-Fi',self.send_music).pack(side='left',padx=(0,10))
        self.btn(row,'Upload / Update USB-C',self.send_music_usb,True).pack(side='left',padx=(0,10))
        self.btn(row,'Delete USB track',self.delete_music_usb).pack(side='left')
        self.progress=ttk.Progressbar(music,maximum=100);self.progress.pack(fill='x',pady=(14,6))
        self.log=ScrolledText(music,bg='#10171b',fg=TEXT,height=8,insertbackground=TEXT,
                              relief='flat',font=('Consolas',10))
        self.log.pack(fill='both',expand=True)
        self.log.insert('end','Wi-Fi: Start Wi-Fi Sync on player and enter the URL shown on its display. 192.168.4.1 is AP fallback only.\nUSB-C (ESP32-S3): select native USB CDC port.\n')
        self.log.configure(state='disabled')
        bottom=tk.Frame(self,bg=BG,padx=22,pady=8);bottom.pack(fill='x')
        self.label(bottom,'MonoPod Studio • standalone offline editor',fg=MUTED,font=('Arial',9)).pack(side='left')
        self.label(bottom,textvariable=self.status_text,fg=ACCENT,font=('Arial',9)).pack(side='right')

    def current_options(self):
        return ThemeOptions(font_path=self.font_path.get().strip(),
            main_size=self.main_size.get(),small_size=self.small_size.get(),
            smoothing=self.smoothing.get(),
            **{name:var.get() for name,var in self.palette.items()})

    def request_preview(self,*_):
        if self._preview_job:self.after_cancel(self._preview_job)
        self._preview_job=self.after(180,self.render_preview)

    def render_preview(self):
        self._preview_job=None
        try:
            im=demo_preview(self.current_options())
            self.preview_photo=ImageTk.PhotoImage(im.resize((480,384),resample=0))
            self.preview_label.configure(image=self.preview_photo)
            self.preview_error.configure(text='')
        except Exception as e:
            self.preview_error.configure(text=str(e))

    def pick_font(self):
        path=filedialog.askopenfilename(title='Select your licensed font',filetypes=[('Fonts','*.ttf *.otf *.ttc'),('All files','*.*')])
        if path:self.font_path.set(path)

    def generate(self):
        data=encode_theme(self.current_options())
        verify_theme(data)
        return data

    def export_theme(self):
        path=filedialog.asksaveasfilename(title='Export MonoPod theme',defaultextension='.mpt',
            initialfile='monopod-theme.mpt',filetypes=[('MonoPod themes','*.mpt')])
        if not path:return
        try:
            data=self.generate();Path(path).write_bytes(data)
            self.status_text.set(f'Exported {len(data)//1024} KiB theme')
        except Exception as e:messagebox.showerror(APP,str(e))

    def save_preview(self):
        path=filedialog.asksaveasfilename(title='Save preview',defaultextension='.png',
            filetypes=[('PNG image','*.png')])
        if path:
            try:demo_preview(self.current_options()).save(path)
            except Exception as e:messagebox.showerror(APP,str(e))

    def save_profile(self):
        path=filedialog.asksaveasfilename(title='Save theme settings',defaultextension='.json',filetypes=[('JSON','*.json')])
        if path:
            try:Path(path).write_text(json.dumps(vars(self.current_options()),indent=2,ensure_ascii=False),'utf8')
            except Exception as e:messagebox.showerror(APP,str(e))

    def load_profile(self):
        path=filedialog.askopenfilename(title='Open theme settings',filetypes=[('JSON','*.json')])
        if not path:return
        try:
            data=json.loads(Path(path).read_text('utf8'))
            for name in ('font_path','main_size','small_size','smoothing'):
                if name in data:getattr(self,name).set(data[name])
            for name,var in self.palette.items():
                if name in data:var.set(data[name])
        except Exception as e:messagebox.showerror(APP,str(e))

    def write_log(self,message):
        self.log.configure(state='normal');self.log.insert('end',message+'\n');self.log.see('end');self.log.configure(state='disabled')

    def async_job(self,title,func):
        self.status_text.set(title)
        def runner():
            try:
                result=func()
                self.after(0,lambda:self.done_job(title,result))
            except Exception as e:
                err=str(e)
                self.after(0,lambda:self.error_job(title,err))
        threading.Thread(target=runner,daemon=True).start()

    def done_job(self,title,result):
        self.status_text.set('Complete')
        self.write_log(f'{title}: {result}')
        self.progress['value']=0

    def error_job(self,title,error):
        self.status_text.set('Error')
        self.write_log(f'{title}: ERROR — {error}')
        self.progress['value']=0
        messagebox.showerror(APP,error)

    def detect_usb(self):
        try:
            ports=get_ports()
            if not ports:raise RuntimeError('No serial devices found; connect native USB port and install driver if required')
            # User can always edit this selection manually.
            picked=next((name for name,desc in ports if 'usb' in desc.lower() or 'jtag' in desc.lower()),ports[0][0])
            self.usb_port.set(picked)
            self.write_log('Serial devices: '+', '.join(f'{a} ({b})' for a,b in ports))
        except Exception as exc:messagebox.showerror(APP,str(exc))

    def connect_usb(self):
        port=self.usb_port.get().strip()
        if not port:messagebox.showerror(APP,'Set a native USB CDC port first');return
        def work():
            with UsbClient(port) as dev:return dev.info()
        def finish(title,info):
            self.device_info.configure(text=f'USB CDC {port} • {info}')
            self.done_job(title,info)
        self._run_custom('USB status',work,finish)

    def send_music_usb(self):
        paths=filedialog.askopenfilenames(title='Select audio files to transfer over USB',
            filetypes=[('Audio','*.mp3 *.wav *.flac *.aac'),('All','*.*')])
        if not paths:return
        port=self.usb_port.get().strip()
        if not port:messagebox.showerror(APP,'Set a native USB CDC port first');return
        def work():
            result=[]
            with UsbClient(port) as dev:
                for filename in paths:
                    def progress(sent,total):
                        percent=int(sent*100/max(1,total))
                        self.after(0,lambda p=percent:self.progress.configure(value=p))
                    result.append(dev.upload(filename,progress=progress,replace=True))
                scan=dev.scan()
            return '\n'.join(result+[scan])
        self.async_job('USB music transfer',work)

    def delete_music_usb(self):
        port=self.usb_port.get().strip()
        if not port:
            messagebox.showerror(APP,'Set a native USB CDC port first');return
        name=simpledialog.askstring(APP,'Enter the exact filename on /Music to delete (including extension):')
        if not name:return
        if not messagebox.askyesno(APP, f'Permanently delete {name!r} from microSD?'):
            return
        def work():
            with UsbClient(port) as dev:
                result=dev.delete(name)
                return result+'\n'+dev.scan()
        self.async_job('USB track deletion',work)

    def connect_device(self):
        url=self.device.get().strip()
        def work():return DeviceClient(url).status()
        def finish(title,info):
            msg=(f"Firmware: {info.get('firmware','?')}\n"
                 f"Songs: {info.get('tracks','?')}  •  Storage: {info.get('sd_used_mb','?')} / {info.get('sd_total_mb','?')} MiB\n"
                 f"Custom theme: {info.get('custom_theme',False)}  •  Font smoothing: {info.get('font_smoothing',False)}")
            self.device_info.configure(text=msg);self.done_job(title,msg)
        self._run_custom('Device status',work,finish)

    def _run_custom(self,title,worker,on_success):
        self.status_text.set(title)
        def runner():
            try:
                result=worker();self.after(0,lambda:on_success(title,result))
            except Exception as e:
                msg=str(e);self.after(0,lambda:self.error_job(title,msg))
        threading.Thread(target=runner,daemon=True).start()

    def push_theme(self):
        try:data=self.generate()
        except Exception as e:messagebox.showerror(APP,str(e));return
        url=self.device.get().strip();smooth=self.smoothing.get()
        def work():
            with tempfile.TemporaryDirectory() as temp:
                path=Path(temp)/'monopod-theme.mpt';path.write_bytes(data)
                response=DeviceClient(url).upload('/api/theme',path)
                DeviceClient(url).form('/api/smoothing',{'enabled':'1' if smooth else '0'})
                return response
        self.async_job('Upload theme',work)

    def push_smoothing(self):
        value=self.smoothing.get();url=self.device.get().strip()
        self.async_job('Apply smoothing',lambda:DeviceClient(url).form('/api/smoothing',{'enabled':'1' if value else '0'}))

    def reset_device_theme(self):
        if not messagebox.askyesno(APP,'Remove custom theme from player? The built-in font remains available.'):
            return
        url=self.device.get().strip()
        self.async_job('Restore font',lambda:DeviceClient(url).form('/api/theme/reset',{}))

    def send_music(self):
        paths=filedialog.askopenfilenames(title='Select music files',filetypes=[('Audio','*.mp3 *.wav *.flac *.aac'),('All','*.*')])
        if not paths:return
        url=self.device.get().strip()
        def work():
            client=DeviceClient(url);done=[]
            for filename in paths:
                path=Path(filename)
                if path.suffix.lower() not in ('.mp3','.wav','.flac','.aac'):
                    raise ValueError(f'Unsupported extension: {path.name}')
                def progress(sent,total):
                    percent=int(sent*100/max(1,total))
                    self.after(0,lambda p=percent:self.progress.configure(value=p))
                client.upload('/upload',path,field='track',progress=progress)
                done.append(path.name)
            return 'Uploaded: '+', '.join(done)
        self.async_job('Music transfer',work)

if __name__=='__main__':
    Studio().mainloop()
