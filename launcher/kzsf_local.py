#!/usr/bin/env python3
"""Local Killzone launcher. Existing saves and prepared game image stay in place."""
from __future__ import annotations
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
from graphics_options import read_ini, write_keys
from prepare import read_sfo

PREFS = ROOT / 'user' / 'launcher-local.json'
CONFIG = ROOT / 'user' / 'launcher-settings.ini'
BG, PANEL, FIELD, LINE = '#090f13', '#101a21', '#192832', '#293e49'
TEXT, MUTED, CYAN, GREEN = '#ecf4f7', '#90a8b7', '#36c6df', '#64dca3'
PROFILES = {'Current build': 'out/gow3-probe-launcher.exe',
            'Beta memory layout (test)': 'out/gow3-probe-launcher.exe'}
BUTTONS = [('cross','Cross / Jump','Space','a'), ('circle','Circle','Left Shift','b'),
           ('square','Square','E','x'), ('triangle','Triangle','Q','y'),
           ('l1','L1','1','leftshoulder'), ('r1','R1','3','rightshoulder'),
           ('l2','L2','R',''), ('r2','R2','F',''), ('l3','L3','Z','leftstick'),
           ('r3','R3','C','rightstick'), ('options','Options','Return','start'),
           ('touchpad_left','Touch left','Tab',''), ('touchpad_right','Touch right','Backspace','back'),
           ('up','D-pad up','I','dpup'), ('down','D-pad down','K','dpdown'),
           ('left','D-pad left','J','dpleft'), ('right','D-pad right','L','dpright')]
PAD_BUTTONS = ('','a','b','x','y','leftshoulder','rightshoulder','leftstick','rightstick',
               'start','back','dpup','dpdown','dpleft','dpright')
DEFAULTS = dict(game_dir=r'C:\Users\User\Desktop\OrbisPort\Killzone', profile='Current build',
                fps='60', window_size='1920x1080', display_mode='windowed', vsync=True,
                rcas=True, show_fps=True, upscaler='none', async_shaders=False,
                stale_readback=True, deferred_readback=True, frames_queued='1',
                unlimited_health=False, infinite_ammo=False,
                controller_look_gain='100', controller_aim_gain='100', controller_turn_speed='100',
                controller_pitch_speed='100', keys={}, pads={})

def load_preferences():
    try:
        data = json.loads(PREFS.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        data = {}
    values = dict(DEFAULTS)
    if isinstance(data, dict):
        values.update({k:v for k,v in data.items() if k in DEFAULTS})
    for key, choices in [('profile', PROFILES), ('fps', ('30','60','120')),
                         ('display_mode', ('windowed','borderless','fullscreen')),
                         ('window_size', ('1280x720','1600x900','1920x1080','2560x1440','3840x2160')),
                         ('upscaler', ('none','fsr3','taa')), ('frames_queued', ('1','2','0'))]:
        if values[key] not in choices:
            values[key] = DEFAULTS[key]
    for key in ('keys','pads'):
        if not isinstance(values[key], dict): values[key] = {}
    settings=read_ini(CONFIG)
    for key in ('controller_look_gain','controller_aim_gain','controller_turn_speed','controller_pitch_speed'):
        value=str(settings.get(key,values[key]))
        values[key]=value if value.isdigit() and 50<=int(value)<=300 else '100'
    return values

def validate_game(folder):
    folder = Path(folder)
    if not (folder / 'eboot.bin').is_file(): return False, 'Choose the game folder containing eboot.bin.'
    try:
        info = read_sfo(folder / 'sce_sys' / 'param.sfo') or {}
        title, version = info.get('TITLE_ID',''), info.get('APP_VER','')
        if title != 'CUSA00190' or version != '01.81':
            return False, f'Expected CUSA00190 / 01.81; found {title or "unknown title"} / {version or "unknown version"}.'
    except (OSError, ValueError, KeyError, IndexError, TypeError) as exc:
        return False, f'Cannot verify game metadata: {exc}'
    return True, 'Ready · Killzone Shadow Fall · CUSA00190 · 01.81'

def persist(values):
    PREFS.parent.mkdir(parents=True, exist_ok=True)
    temp = PREFS.with_suffix('.tmp')
    temp.write_text(json.dumps(values, indent=2)+'\n', encoding='utf-8')
    temp.replace(PREFS)
    if not CONFIG.exists():
        CONFIG.write_text((ROOT/'gow3.ini').read_text(encoding='utf-8') if (ROOT/'gow3.ini').exists() else '', encoding='utf-8')
    updates = {k:str(int(values[k])) for k in ('vsync','rcas','show_fps','async_shaders','stale_readback','deferred_readback')}
    updates.update(display_mode=values['display_mode'], fps_limit=values['fps'],
                   frames_queued=values['frames_queued'], upscaler=values['upscaler'],
                   controller_look_gain=values['controller_look_gain'],
                   controller_aim_gain=values['controller_aim_gain'],
                   controller_turn_speed=values['controller_turn_speed'],
                   controller_pitch_speed=values['controller_pitch_speed'])
    write_keys(CONFIG, updates)

def executable_for(values):
    # Features are opt-in flags in one current build, so changing assists or
    # controls cannot silently select an older overlay/renderer executable.
    return PROFILES[values['profile']]

def launch_spec(values, folder):
    env = dict(os.environ)
    for key in ('KZSF_ORIGINAL_CLOCK','KZSF_UNIFIED_TSC','KZSF_FILE_TRACE','KZSF_RWLOCK_TRACE',
                'GOW3_TRACE_IMPORTS','GOW3_TSC_HZ','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE',
                'GOW3_PAD_FILE','GOW3_NO_MOVIES','GOW3_AUDIO','GOW3_AUDIO_DUMP','GOW3_WRITE_LOG',
                'GOW3_USER_DIR','GOW3_GPU_USER_DIR','GOW3_DATA_DIR','GOW3_FREEZE_EXIT','KZSF_ENGINE_TURN',
                'KZSF_TURN_LOG','KZSF_PLAYER_TURN','KZSF_FSR_NATIVE','KZSF_FSR_QUALITY','GOW3_RENDER_RES',
                'GOW3_PRESET_FILE','GOW3_SCENE_DEBUG','KZSF_DIRECT_TEXTURE_MEMO'):
        env.pop(key, None)
    beta = values['profile'] == 'Beta memory layout (test)'
    env.update(PATH=r'C:\msys64\clang64\bin'+';'+env.get('PATH',''),
        KZSF_VIRTUAL_TSC='1', KZSF_BETA_MEMORY_LAYOUT='1' if beta else '0',
        KZSF_UNLIMITED_HEALTH=str(int(values.get('unlimited_health',False))),
        KZSF_INFINITE_AMMO=str(int(values.get('infinite_ammo',False))),
        GOW3_DMEM_MB='4608' if beta else '6144', GOW3_FPS_LIMIT=values['fps'], GOW3_VBLANK_HZ='60',
        GOW3_VK_VALIDATION='0', GOW3_VK_VALIDATION_SYNC='0', GOW3_VK_STOP_ON_ERROR='0',
        VK_LOADER_LAYERS_DISABLE='~implicit~', GOW3_VK_RECORD_THREAD='0', GOW3_DRAW_PIPE='0',
        GOW3_FULL_BARRIERS='0', GOW3_TOGGLE_FILE=str(ROOT/'out/renderer-stability-toggles.txt'),
        GOW3_GPU_LOG='error', GOW3_AUDIO_STATS='1', GOW3_FRAME_STATS='1',
        GOW3_FREEZE_SECONDS='15', GOW3_STALL_SECONDS='60',
        GOW3_WINDOW_FOCUS='1', GOW3_CONFIG=str(CONFIG), GOW3_WINDOW_SIZE=values['window_size'],
        GOW3_USER_DIR=str(ROOT/'user'), GOW3_GPU_USER_DIR=str(ROOT/'user'),
        GOW3_ASYNC_SHADERS=str(int(values['async_shaders'])), GOW3_UPSCALER=values['upscaler'],
        GOW3_READBACK_LINEAR='1',
        GOW3_PAD_LOOK_GAIN=str(values.get('controller_look_gain','100')),
        GOW3_PAD_AIM_GAIN=str(values.get('controller_aim_gain','100')),
        GOW3_PAD_TURN_SPEED=str(values.get('controller_turn_speed','100')),
        GOW3_PAD_PITCH_SPEED=str(values.get('controller_pitch_speed','100')),
        GOW3_DEFERRED_READBACK=str(int(values['deferred_readback'])),
        GOW3_STALE_READBACK=str(int(values['stale_readback'])), GOW3_FRAMES_AHEAD=values['frames_queued'])
    env['GOW3_KEY_MAP'] = ','.join(f'{k}={v}' for k,v in values['keys'].items() if v)
    env['GOW3_PAD_MAP'] = ','.join(f'{k}={v}' for k,v in values['pads'].items() if v)
    executable=executable_for(values)
    env.update(KZSF_DIRECT_TSC='1',GOW3_PRECOMPILE='1',GOW3_BACKGROUND_WARMUP='0',GOW3_FREEZE_SECONDS='0')
    if any(str(values.get(k,'100'))!='100' for k in ('controller_turn_speed','controller_pitch_speed')):
        env['KZSF_PLAYER_TURN']='1'
    if values['upscaler'] in ('fsr3','taa'):
        env.update(KZSF_FSR_NATIVE='1',GOW3_UPSCALE_PRESET='0',GOW3_OUTPUT_RES='1920x1080',
                   GOW3_JITTER='0',GOW3_REACTIVE='0',GOW3_OBJECT_MOTION='0')
    command = [str(ROOT/executable), 'out/boot-linked.bin', '--content-profile',
               'out/content.bin', '--app0', values['game_dir']]
    return dict(command=command, env=env, cwd=str(ROOT), folder=str(folder))

def run_session(manifest):
    """Independent monitor retains exit status even if the launcher window closes."""
    spec = json.loads(Path(manifest).read_text(encoding='utf-8'))
    folder = Path(spec['folder'])
    result = {'started':time.strftime('%Y-%m-%d %H:%M:%S')}
    try:
        with (folder/'stdout.log').open('wb') as out, (folder/'stderr.log').open('wb') as err:
            game = subprocess.Popen(spec['command'], env=spec.get('env',os.environ), cwd=spec['cwd'], stdout=out, stderr=err,
                                    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            result['pid'] = game.pid
            (folder/'status.json').write_text(json.dumps(result), encoding='utf-8')
            result['exit_code'] = game.wait()
    except Exception as exc:
        result.update(exit_code=-1, error=str(exc))
    result['finished'] = time.strftime('%Y-%m-%d %H:%M:%S')
    (folder/'status.json').write_text(json.dumps(result), encoding='utf-8')
    with (folder/'run.txt').open('a',encoding='utf-8') as log: log.write(f'Exit code: {result["exit_code"]}\n')

class Launcher:
    def __init__(self, root):
        self.root, self.values = root, load_preferences()
        self.monitor = None
        self.session = None
        root.title('Killzone Shadow Fall · PC Launcher')
        try:
            self.icon=tk.PhotoImage(file=str(Path(self.values['game_dir'])/'sce_sys/icon0.png'))
            root.iconphoto(True,self.icon)
        except tk.TclError: pass
        root.geometry('1000x820'); root.minsize(960,780); root.configure(bg=BG)
        root.option_add('*Font', ('Segoe UI',10))
        self.vars = {k:(tk.BooleanVar(value=v) if isinstance(v,bool) else tk.StringVar(value=v))
                     for k,v in self.values.items() if k not in ('keys','pads')}
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TCombobox', fieldbackground=FIELD, background=LINE, foreground=TEXT, arrowcolor=CYAN, padding=5)
        style.map('TCombobox', fieldbackground=[('readonly',FIELD)], foreground=[('readonly',TEXT)])
        style.configure('TCheckbutton', background=PANEL, foreground=TEXT, indicatorbackground=FIELD)
        style.map('TCheckbutton', background=[('active',PANEL)])
        self.banner = tk.Canvas(root,height=170,bg=BG,highlightthickness=0); self.banner.pack(fill='x')
        self.banner.bind('<Configure>', self.draw_banner)
        bar = tk.Frame(root,bg=BG); bar.pack(fill='x',padx=28)
        self.tab_buttons = {}
        for name in ('Game','Graphics','Controls','Cheats','Advanced'):
            b = self.button(bar,name.upper(),lambda n=name:self.show_tab(n)); b.pack(side='left',pady=8,padx=(0,5)); self.tab_buttons[name]=b
        footer=tk.Frame(root,bg=BG); footer.pack(side='bottom',fill='x',padx=28,pady=16)
        tk.Label(footer,text='LOCAL BUILD  /  YOUR DUMP · YOUR SAVES',bg=BG,fg=MUTED,font=('Segoe UI',9)).pack(side='left')
        self.play_button=self.button(footer,'PLAY',self.play,primary=True); self.play_button.pack(side='right',ipadx=35,ipady=8)
        self.save_button=self.button(footer,'Save settings',self.save); self.save_button.pack(side='right',padx=12)
        self.body=tk.Frame(root,bg=PANEL,highlightbackground=LINE,highlightthickness=1)
        self.body.pack(fill='both',expand=True,padx=28)
        self.pages={}
        for name in self.tab_buttons:
            page=tk.Frame(self.body,bg=PANEL,padx=26,pady=22); self.pages[name]=page
        self.build_game(); self.build_graphics(); self.build_controls(); self.build_cheats(); self.build_advanced()
        self.show_tab('Game'); self.refresh_ready(); self.poll()
        root.protocol('WM_DELETE_WINDOW',self.close)

    def label(self,parent,text,muted=False):
        return tk.Label(parent,text=text,bg=PANEL,fg=MUTED if muted else TEXT,anchor='w',justify='left',wraplength=840)
    def button(self,parent,text,command,primary=False):
        return tk.Button(parent,text=text,command=command,bg=CYAN if primary else FIELD,fg=BG if primary else TEXT,
                         activebackground='#58d9ec',activeforeground=BG,relief='flat',bd=0,padx=14,pady=8,cursor='hand2')
    def heading(self,parent,text):
        tk.Label(parent,text=text.upper(),bg=PANEL,fg=CYAN,font=('Segoe UI',10,'bold'),anchor='w').pack(fill='x',pady=(0,12))
    def choice(self,parent,label,key,options):
        row=tk.Frame(parent,bg=PANEL); row.pack(fill='x',pady=7)
        self.label(row,label).pack(side='left',fill='x',expand=True)
        box=ttk.Combobox(row,textvariable=self.vars[key],values=list(options),state='readonly',width=30)
        box.pack(side='right'); return box
    def check(self,parent,label,key):
        ttk.Checkbutton(parent,text=label,variable=self.vars[key]).pack(anchor='w',pady=8)
    def show_tab(self,name):
        for page in self.pages.values(): page.pack_forget()
        self.pages[name].pack(fill='both',expand=True)
        for key,b in self.tab_buttons.items(): b.configure(fg=CYAN if key==name else MUTED)

    def draw_banner(self,event=None):
        c=self.banner; c.delete('all'); w=max(c.winfo_width(),960)
        try:
            from PIL import Image, ImageOps, ImageTk
            source=Path(self.vars['game_dir'].get())/'sce_sys'/'pic0.png'
            if not source.exists(): source=source.with_name('pic1.png')
            if not source.exists(): source=source.with_name('icon0.png')
            with Image.open(source) as im: im=ImageOps.fit(im.convert('RGB'),(w,170))
            shade=Image.new('RGBA',im.size,(3,13,19,155)); im=Image.alpha_composite(im.convert('RGBA'),shade)
            self.art=ImageTk.PhotoImage(im); c.create_image(0,0,image=self.art,anchor='nw')
        except (OSError,ImportError):
            for x in range(0,w,8): c.create_rectangle(x,0,x+8,170,fill='#0d202b' if x%48 else '#123443',outline='')
        c.create_text(30,55,text='KILLZONE',anchor='w',fill=TEXT,font=('Segoe UI Black',35,'bold'))
        c.create_text(33,101,text='S H A D O W   F A L L',anchor='w',fill=TEXT,font=('Segoe UI Light',18))
        c.create_line(33,130,96,130,fill=CYAN,width=3)
        c.create_text(109,130,text='PC  /  LOCAL BUILD',anchor='w',fill=CYAN,font=('Segoe UI',9,'bold'))

    def build_game(self):
        p=self.pages['Game']; self.heading(p,'Game folder')
        row=tk.Frame(p,bg=PANEL); row.pack(fill='x')
        entry=tk.Entry(row,textvariable=self.vars['game_dir'],bg=FIELD,fg=TEXT,insertbackground=CYAN,relief='flat')
        entry.pack(side='left',fill='x',expand=True,ipady=9)
        self.button(row,'Browse…',self.browse).pack(side='right',padx=(10,0))
        self.ready=self.label(p,''); self.ready.pack(fill='x',pady=(12,24))
        entry.bind('<FocusOut>',lambda e:self.refresh_ready())
        self.choice(p,'Game build','profile',PROFILES)
        self.label(p,'Current build keeps the established memory layout. Beta memory layout tests the imported heap fix.',True).pack(fill='x',pady=(5,28))
        self.heading(p,'Folders')
        row=tk.Frame(p,bg=PANEL); row.pack(fill='x')
        for name,path in [('Saved games',ROOT/'user/savedata/1/CUSA00190/KZ4'),('Screenshots',ROOT/'user/screenshots'),('Logs',ROOT/'out')]:
            self.button(row,name,lambda f=path:self.open_folder(f)).pack(side='left',padx=(0,9))
        self.label(p,'Each launch saves a separate log folder. Existing saved games are shared by both builds.',True).pack(fill='x',pady=15)
        self.session_label=self.label(p,'Ready to launch.'); self.session_label.pack(fill='x',pady=12)

    def build_graphics(self):
        p=self.pages['Graphics']; self.heading(p,'Display')
        for label,key,options in [('Display mode','display_mode',('windowed','borderless','fullscreen')),
          ('Window size','window_size',('1280x720','1600x900','1920x1080','2560x1440','3840x2160')),
          ('Frame rate limit','fps',('30','60','120')),('Temporal upscaler / AA','upscaler',('none','fsr3','taa'))]: self.choice(p,label,key,options)
        self.label(p,'Window size changes output size. The game renders internally at its configured resolution.',True).pack(fill='x',pady=12)
        self.check(p,'VSync','vsync'); self.check(p,'Sharpening (RCAS)','rcas'); self.check(p,'Show FPS','show_fps')
        self.label(p,'FPS values are limits; performance depends on the scene. FSR 3 and TAA are experimental. TAA filters native 1080p; it does not increase FPS.',True).pack(fill='x',pady=15)

    def build_controls(self):
        page=self.pages['Controls']
        canvas=tk.Canvas(page,bg=PANEL,highlightthickness=0,height=430)
        scroll=ttk.Scrollbar(page,orient='vertical',command=canvas.yview)
        scroll.pack(side='right',fill='y'); canvas.pack(side='left',fill='both',expand=True)
        canvas.configure(yscrollcommand=scroll.set)
        p=tk.Frame(canvas,bg=PANEL)
        window=canvas.create_window((0,0),window=p,anchor='nw')
        p.bind('<Configure>',lambda event:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda event:canvas.itemconfigure(window,width=event.width))
        def wheel(event):
            if canvas.winfo_ismapped() and event.widget.winfo_class()!='TCombobox':
                canvas.yview_scroll(-int(event.delta/120),'units')
        self.root.bind('<MouseWheel>',wheel,add='+')
        self.heading(p,'Keyboard and controller')
        gains=('50','75','100','125','150','175','200','250','300')
        self.choice(p,'Engine turn speed (experimental, %)','controller_turn_speed',gains)
        self.choice(p,'Engine pitch speed (experimental, %)','controller_pitch_speed',gains)
        self.choice(p,'Controller look response (%)','controller_look_gain',gains)
        self.choice(p,'Controller aim response / L2 (%)','controller_aim_gain',gains)
        self.label(p,'100% = original. Higher values reach full input sooner; maximum turn speed stays unchanged.',True).pack(fill='x',pady=(0,8))
        self.label(p,'Movement: WASD · Camera: arrow keys · Insert: port menu\nSelect a keyboard field and press a key. Esc cancels. Sticks and controller triggers retain their native bindings.',True).pack(fill='x',pady=(0,16))
        table=tk.Frame(p,bg=PANEL); table.pack(fill='x')
        self.key_vars={}; self.pad_vars={}
        for index,(key,label,keyboard,pad) in enumerate(BUTTONS):
            col=(index//9)*3; row=index%9
            self.label(table,label).grid(row=row,column=col,sticky='w',padx=(0,10),pady=4)
            kv=tk.StringVar(value=self.values['keys'].get(key,keyboard)); self.key_vars[key]=kv
            b=self.button(table,kv.get(),lambda k=key:self.capture_key(k)); b.configure(width=10,pady=3)
            b.grid(row=row,column=col+1,padx=(0,6)); setattr(b,'key_var',kv)
            kv.trace_add('write',lambda *_,v=kv,button=b:button.configure(text=v.get()))
            pv=tk.StringVar(value=self.values['pads'].get(key,pad)); self.pad_vars[key]=pv
            box=ttk.Combobox(table,textvariable=pv,values=PAD_BUTTONS,state='readonly',width=13)
            box.grid(row=row,column=col+2,padx=(0,20))
            if key in ('l2','r2'): box.configure(state='disabled')
        self.button(p,'Restore control defaults',self.reset_controls).pack(anchor='w',pady=20)
        self.label(p,'A blank controller field keeps the built-in mapping. Changes apply on the next launch.',True).pack(fill='x')

    def capture_key(self,key):
        old=self.key_vars[key].get(); self.key_vars[key].set('Press key…')
        popup=tk.Toplevel(self.root); popup.title('Choose key'); popup.configure(bg=PANEL); popup.geometry('320x100')
        self.label(popup,'Press a keyboard key. Esc cancels.').pack(padx=15,pady=25)
        popup.transient(self.root); popup.grab_set(); popup.focus_force()
        def finish(value): self.key_vars[key].set(value); popup.destroy()
        def press(event):
            names={'space':'Space','Return':'Return','Shift_L':'Left Shift','Shift_R':'Right Shift',
                   'Control_L':'Left Ctrl','Control_R':'Right Ctrl','Alt_L':'Left Alt','Alt_R':'Right Alt',
                   'Prior':'PageUp','Next':'PageDown'}
            if event.keysym=='Escape': finish(old); return
            value=names.get(event.keysym,event.keysym.upper() if len(event.keysym)==1 else event.keysym)
            if any(c in value for c in '=,'): return
            finish(value)
        popup.bind('<KeyPress>',press); popup.protocol('WM_DELETE_WINDOW',lambda:finish(old))
    def reset_controls(self):
        self.vars['controller_turn_speed'].set('100')
        self.vars['controller_pitch_speed'].set('100')
        self.vars['controller_look_gain'].set('100'); self.vars['controller_aim_gain'].set('100')
        for key,label,keyboard,pad in BUTTONS: self.key_vars[key].set(keyboard); self.pad_vars[key].set(pad)

    def build_advanced(self):
        page=self.pages['Advanced']
        p=tk.Frame(page,bg=PANEL); p.pack(fill='both',expand=True)
        self.heading(p,'Rendering')
        self.check(p,'Compile new shaders in the background','async_shaders')
        note=self.label(p,'Experimental; may cause missing draws or instability.',True)
        note.configure(wraplength=420); note.pack(fill='x',pady=(0,12))
        self.check(p,'GPU readbacks without waiting','stale_readback')
        self.check(p,'Defer GPU readbacks','deferred_readback')
        self.choice(p,'Frames queued (0 = automatic)','frames_queued',('1','2','0'))
        note=self.label(p,'Changes apply on the next launch. Background compilation defaults off for stability.',True)
        note.configure(wraplength=420); note.pack(fill='x',pady=22)
        self.button(p,'Open latest session logs',lambda:self.open_folder(self.session or ROOT/'out')).pack(anchor='w')

    def build_cheats(self):
        p=self.pages['Cheats']
        self.heading(p,'Campaign cheats')
        self.check(p,'Unlimited player health','unlimited_health')
        self.label(p,'Protects the player from normal damage, including bullets and explosions.',True).pack(fill='x',pady=(0,12))
        self.check(p,'Infinite ammo / no reload','infinite_ammo')
        self.label(p,'Uses the existing Killzone ammo patch.',True).pack(fill='x',pady=(0,20))
        self.label(p,'Changes apply on the next launch. Turn a cheat off and relaunch to restore normal play. Scripted deaths may still occur.',True).pack(fill='x',pady=12)
        self.button(p,'Disable all cheats',self.reset_cheats).pack(anchor='w',pady=12)

    def reset_cheats(self):
        self.vars['unlimited_health'].set(False)
        self.vars['infinite_ammo'].set(False)

    def gather(self):
        values={k:v.get() for k,v in self.vars.items()}
        values['keys']={k:v.get() for k,v in self.key_vars.items()}
        values['pads']={k:v.get() for k,v in self.pad_vars.items()}
        return values
    def save(self):
        try: persist(self.gather()); self.session_label.configure(text='Settings saved. Changes apply on the next launch.',fg=GREEN)
        except OSError as exc: messagebox.showerror('Cannot save settings',str(exc),parent=self.root)
    def browse(self):
        folder=filedialog.askdirectory(initialdir=self.vars['game_dir'].get(),parent=self.root)
        if folder: self.vars['game_dir'].set(folder); self.refresh_ready(); self.draw_banner()
    def refresh_ready(self):
        ok,text=validate_game(self.vars['game_dir'].get()); self.ready.configure(text=text,fg=GREEN if ok else '#e8b969')
        self.game_ok=ok
    def open_folder(self,path):
        path.mkdir(parents=True,exist_ok=True); os.startfile(str(path))
    def play(self):
        if self.monitor and self.monitor.poll() is None: return
        if os.name=='nt':
            running=subprocess.run(['tasklist','/FI','IMAGENAME eq gow3-probe*','/FO','CSV','/NH'],
                capture_output=True,text=True,errors='replace',creationflags=subprocess.CREATE_NO_WINDOW)
            if 'gow3-probe' in running.stdout.lower():
                messagebox.showinfo('Game already running','Close the current Killzone game before starting another session.',parent=self.root); return
        self.refresh_ready()
        if not self.game_ok: self.show_tab('Game'); return
        values=self.gather()
        exe=ROOT/executable_for(values)
        for path in (exe,ROOT/'out/boot-linked.bin',ROOT/'out/content.bin',ROOT/'out/renderer-stability-toggles.txt'):
            if not path.is_file(): messagebox.showerror('Missing game build',f'Missing: {path}',parent=self.root); return
        try:
            persist(values)
            folder=ROOT/'out'/('dialogue-'+time.strftime('%Y%m%d-%H%M%S')+f'-{time.time_ns()%1000000:06d}-modelauncher')
            folder.mkdir(parents=True); self.session=folder
            spec=launch_spec(values,folder)
            # Environment is passed directly, never persisted (it may contain credentials).
            manifest={k:v for k,v in spec.items() if k!='env'}
            (folder/'session.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
            (folder/'run.txt').write_text(f'Mode: launcher\nStarted: {time.strftime("%Y-%m-%d %H:%M:%S")}\nBuild: {values["profile"]}\nFPS limit: {values["fps"]}; virtual PS4 clock: 1\n',encoding='utf-8')
            with (folder/'run.txt').open('a',encoding='utf-8') as log:
                for key in ('KZSF_BETA_MEMORY_LAYOUT','GOW3_DMEM_MB','GOW3_DRAW_PIPE',
                            'GOW3_VK_RECORD_THREAD','GOW3_VK_VALIDATION','GOW3_ASYNC_SHADERS',
                            'GOW3_UPSCALER','GOW3_READBACK_LINEAR','GOW3_DEFERRED_READBACK','GOW3_STALE_READBACK',
                            'KZSF_UNLIMITED_HEALTH','KZSF_INFINITE_AMMO'):
                    log.write(f'{key}={spec["env"][key]}\n')
            self.monitor=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--run-session',str(folder/'session.json')],
                env=spec['env'],cwd=ROOT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.play_button.configure(text='RUNNING',state='disabled'); self.session_label.configure(text='Game running · '+folder.name,fg=CYAN)
        except (OSError,ValueError) as exc: messagebox.showerror('Launch failed',str(exc),parent=self.root)
    def poll(self):
        if self.monitor and self.monitor.poll() is not None:
            try: result=json.loads((self.session/'status.json').read_text()); text=f'Game exited · code {result.get("exit_code", "unknown")}'
            except (OSError,ValueError): text='Game session ended · see logs'
            self.session_label.configure(text=text,fg=MUTED); self.play_button.configure(text='PLAY',state='normal'); self.monitor=None
        self.root.after(1000,self.poll)
    def close(self):
        try: persist(self.gather())
        except OSError: pass
        self.root.destroy()

def main():
    if len(sys.argv)>2 and sys.argv[1]=='--run-session':
        # Child monitor inherits the launch environment, not a copy written to disk.
        run_session(sys.argv[2]); return
    if os.name=='nt':
        import ctypes
        try: ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except OSError: pass
    root=tk.Tk(); Launcher(root); root.mainloop()

if __name__=='__main__': main()
