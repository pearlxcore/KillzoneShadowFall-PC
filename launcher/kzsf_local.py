#!/usr/bin/env python3
"""Killzone Shadow Fall launcher and independent game-session monitor."""
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
BG, PANEL, FIELD, LINE = '#111214', '#1a1c1f', '#26292d', '#34373c'
TEXT, MUTED, CYAN, GREEN = '#f3f0eb', '#a3a5aa', '#f48b55', '#94c7a2'
OPTION_LABELS = {
    'look_curve': {'0':'Original', '1':'Linear (custom)', '2':'Precision (custom)'},
    'profile': {'Current build': 'Standard', 'Beta memory layout (test)': 'Compatibility (experimental)'},
    'upscaler': {'none': 'Native', 'fsr3': 'FSR 3.1 AA (experimental)', 'taa': 'TAA (experimental)'},
    'display_mode': {'windowed': 'Windowed', 'borderless': 'Borderless', 'fullscreen': 'Fullscreen'},
    'frames_queued': {'0': 'Automatic', '1': '1 frame', '2': '2 frames'},
}
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
PAD_LABELS = {'': 'Default', 'a': 'A / Cross', 'b': 'B / Circle', 'x': 'X / Square',
              'y': 'Y / Triangle', 'leftshoulder': 'LB / L1', 'rightshoulder': 'RB / R1',
              'leftstick': 'LS / L3', 'rightstick': 'RS / R3', 'start': 'Start / Options',
              'back': 'Back / Share', 'dpup': 'D-pad up', 'dpdown': 'D-pad down',
              'dpleft': 'D-pad left', 'dpright': 'D-pad right'}
DEFAULTS = dict(game_dir='', profile='Current build',
                fps='60', window_size='1920x1080', display_mode='windowed', vsync=True,
                rcas=True, show_fps=True, upscaler='none', async_shaders=False,
                stale_readback=True, deferred_readback=True, frames_queued='1',
                unlimited_health=False, infinite_ammo=False,
                controller_look_gain='100', controller_aim_gain='100', controller_turn_speed='100',
                controller_pitch_speed='100', keys={}, pads={})
DEFAULTS.update(mouse_aim=True,mouse_sensitivity='100',mouse_ads_sensitivity='100',mouse_invert_y=False)
DEFAULTS.update(motion_blur=True)
DEFAULTS.update(mouse_horizontal_speed='100',mouse_vertical_speed='100')
DEFAULTS.update(sprint_turn_speed='100')
DEFAULTS.update(look_curve='1',look_deadzone='8',look_ramp=False,look_assist=True)
DEFAULTS.update(controller_drift_deadzone='8')
LIVE_CONFIG = {'fps':'fps_limit', 'upscaler':'upscaler', 'display_mode':'display_mode',
    **{key:key for key in ('vsync','rcas','show_fps','async_shaders','stale_readback',
       'deferred_readback','frames_queued','mouse_aim','mouse_invert_y','mouse_sensitivity',
       'mouse_ads_sensitivity','mouse_horizontal_speed','mouse_vertical_speed','motion_blur',
       'unlimited_health','infinite_ammo','controller_look_gain','controller_aim_gain',
       'controller_turn_speed','controller_pitch_speed','sprint_turn_speed',
       'look_curve','look_deadzone','look_ramp','look_assist','controller_drift_deadzone')}}

def load_preferences():
    try:
        data = json.loads(PREFS.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        data = {}
    values = dict(DEFAULTS)
    if isinstance(data, dict):
        values.update({k:v for k,v in data.items() if k in DEFAULTS})
    settings=read_ini(CONFIG)
    for key,ini_key in LIVE_CONFIG.items():
        if ini_key in settings:
            value=settings[ini_key]
            if isinstance(DEFAULTS[key],bool):
                if value in ('0','1'):values[key]=value=='1'
            else:values[key]='none' if key=='upscaler' and value=='off' else value
    for key, choices in [('profile', PROFILES), ('fps', ('30','60','120')),
                         ('display_mode', ('windowed','borderless','fullscreen')),
                         ('window_size', ('1280x720','1600x900','1920x1080','2560x1440','3840x2160')),
                         ('upscaler', ('none','fsr3','taa')), ('frames_queued', ('1','2','0'))]:
        if values[key] not in choices:
            values[key] = DEFAULTS[key]
    for key in ('keys','pads'):
        if not isinstance(values[key], dict): values[key] = {}
    settings=read_ini(CONFIG)
    mouse_gain=str(values.get('mouse_sensitivity','100'))
    values['mouse_sensitivity']=mouse_gain if mouse_gain.isdigit() and 50<=int(mouse_gain)<=300 else '100'
    ads_gain=str(settings.get('mouse_ads_sensitivity',data.get('mouse_ads_sensitivity',values['mouse_sensitivity']))) if isinstance(data,dict) else '100'
    values['mouse_ads_sensitivity']=ads_gain if ads_gain.isdigit() and 25<=int(ads_gain)<=300 else '100'
    for key in ('mouse_horizontal_speed','mouse_vertical_speed'):
        value=str(values[key])
        values[key]=value if value.isdigit() and 25<=int(value)<=300 else '100'
    for key in ('controller_look_gain','controller_aim_gain','controller_turn_speed','controller_pitch_speed'):
        value=str(settings.get(key,values[key]))
        values[key]=value if value.isdigit() and 50<=int(value)<=300 else '100'
    sprint=str(values.get('sprint_turn_speed','100'))
    values['sprint_turn_speed']=sprint if sprint.isdigit() and 100<=int(sprint)<=500 else '100'
    if str(values['look_curve']) not in ('0','1','2'):values['look_curve']='0'
    deadzone=str(values['look_deadzone'])
    values['look_deadzone']=deadzone if deadzone.isdigit() and 0<=int(deadzone)<=30 else '8'
    drift=str(values['controller_drift_deadzone'])
    values['controller_drift_deadzone']=drift if drift.isdigit() and 0<=int(drift)<=30 else '8'
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
                   controller_pitch_speed=values['controller_pitch_speed'],
                   sprint_turn_speed=values.get('sprint_turn_speed','100'))
    for key in ('mouse_aim','mouse_invert_y','mouse_sensitivity','mouse_ads_sensitivity',
                'mouse_horizontal_speed','mouse_vertical_speed','motion_blur','unlimited_health','infinite_ammo',
                'look_curve','look_deadzone','look_ramp','look_assist','controller_drift_deadzone'):
        value=values.get(key,DEFAULTS[key])
        updates[key]=str(int(value)) if isinstance(value,bool) else str(value)
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
        KZSF_DISABLE_MOTION_BLUR=str(int(not values.get('motion_blur',True))),
        GOW3_DMEM_MB='4608' if beta else '6144', GOW3_FPS_LIMIT=values['fps'], GOW3_VBLANK_HZ='60',
        GOW3_VK_VALIDATION='0', GOW3_VK_VALIDATION_SYNC='0', GOW3_VK_STOP_ON_ERROR='0',
        VK_LOADER_LAYERS_DISABLE='~implicit~', GOW3_VK_RECORD_THREAD='0', GOW3_DRAW_PIPE='0',
        GOW3_FULL_BARRIERS='0', GOW3_TOGGLE_FILE=str(ROOT/'out/renderer-stability-toggles.txt'),
        GOW3_GPU_LOG='error', GOW3_AUDIO_STATS='1', GOW3_FRAME_STATS='1',
        GOW3_FREEZE_SECONDS='15', GOW3_STALL_SECONDS='60',
        GOW3_WINDOW_FOCUS='1', GOW3_CONFIG=str(CONFIG), GOW3_WINDOW_SIZE=values['window_size'],
        GOW3_USER_DIR=str(ROOT/'user'), GOW3_GPU_USER_DIR=str(ROOT/'user'),
        GOW3_ASYNC_SHADERS=str(int(values['async_shaders'])), GOW3_UPSCALER=values['upscaler'],
        GOW3_READBACK_LINEAR='1', GOW3_RCAS=str(int(values['rcas'])),
        GOW3_MOUSE_AIM=str(int(values.get('mouse_aim',True))),
        GOW3_MOUSE_SENSITIVITY=str(values.get('mouse_sensitivity','100')),
        GOW3_MOUSE_ADS_SENSITIVITY=str(values.get('mouse_ads_sensitivity',values.get('mouse_sensitivity','100'))),
        GOW3_MOUSE_HORIZONTAL_SPEED=str(values.get('mouse_horizontal_speed','100')),
        GOW3_MOUSE_VERTICAL_SPEED=str(values.get('mouse_vertical_speed','100')),
        GOW3_MOUSE_INVERT_Y=str(int(values.get('mouse_invert_y',False))),
        GOW3_PAD_LOOK_GAIN=str(values.get('controller_look_gain','100')),
        KZSF_PAD_DRIFT_DEADZONE=str(values.get('controller_drift_deadzone','8')),
        GOW3_PAD_AIM_GAIN=str(values.get('controller_aim_gain','100')),
        GOW3_PAD_TURN_SPEED=str(values.get('controller_turn_speed','100')),
        KZSF_SPRINT_TURN_SPEED=str(values.get('sprint_turn_speed','100')),
        KZSF_LOOK_CURVE=str(values.get('look_curve','1')),
        KZSF_LOOK_DEADZONE=str(values.get('look_deadzone','8')),
        KZSF_LOOK_RAMP=str(int(values.get('look_ramp',False))),
        KZSF_LOOK_ASSIST=str(int(values.get('look_assist',True))),
        GOW3_PAD_PITCH_SPEED=str(values.get('controller_pitch_speed','100')),
        GOW3_DEFERRED_READBACK=str(int(values['deferred_readback'])),
        GOW3_STALE_READBACK=str(int(values['stale_readback'])), GOW3_FRAMES_AHEAD=values['frames_queued'])
    env['GOW3_KEY_MAP'] = ','.join(f'{k}={v}' for k,v in values['keys'].items() if v)
    env['GOW3_PAD_MAP'] = ','.join(f'{k}={v}' for k,v in values['pads'].items() if v)
    executable=executable_for(values)
    env.update(KZSF_DIRECT_TSC='1',GOW3_PRECOMPILE='1',GOW3_BACKGROUND_WARMUP='0',GOW3_FREEZE_SECONDS='0')
    # Keep the guarded player hook available for live overlay adjustments from 100%.
    env['KZSF_PLAYER_TURN']='1'
    if values['upscaler'] in ('fsr3','taa'):
        env.update(KZSF_FSR_NATIVE='1',GOW3_UPSCALE_PRESET='0',GOW3_OUTPUT_RES='1920x1080',
                   GOW3_JITTER='0',GOW3_REACTIVE='0',GOW3_OBJECT_MOTION='0')
    command = [str(ROOT/executable), 'out/boot-linked.bin', '--content-profile',
               'out/content.bin', '--app0', values['game_dir']]
    return dict(command=command, env=env, cwd=str(ROOT), folder=str(folder))

def session_window_icon(command, folder):
    """Provide the launcher artwork to SDL, with the dump icon as a fallback."""
    if '--app0' not in command:
        return None
    from PIL import Image
    source = ROOT / 'launcher' / 'killzone.png'
    if not source.is_file():
        source = Path(command[command.index('--app0') + 1]) / 'sce_sys' / 'icon0.png'
    destination = Path(folder) / 'window-icon.bmp'
    with Image.open(source) as original:
        original.convert('RGB').resize((128, 128), Image.Resampling.LANCZOS).save(destination)
    return destination.resolve()


def run_session(manifest):
    """Independent monitor retains exit status even if the launcher window closes."""
    spec = json.loads(Path(manifest).read_text(encoding='utf-8'))
    folder = Path(spec['folder'])
    result = {'started':time.strftime('%Y-%m-%d %H:%M:%S')}
    try:
        with (folder/'stdout.log').open('wb') as out, (folder/'stderr.log').open('wb') as err:
            env = dict(spec.get('env', os.environ))
            env.pop('GOW3_WINDOW_ICON', None)
            try:
                icon = session_window_icon(spec['command'], folder)
                if icon:
                    env['GOW3_WINDOW_ICON'] = str(icon)
            except (ImportError, OSError, ValueError, IndexError) as exc:
                err.write(f'Window icon unavailable: {exc}\n'.encode('utf-8'))
                err.flush()
            game = subprocess.Popen(spec['command'], env=env, cwd=spec['cwd'], stdout=out, stderr=err,
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
        root.title('Killzone Shadow Fall')
        try:
            self.icon=tk.PhotoImage(file=str(ROOT/'launcher/killzone.png'))
            root.iconphoto(True,self.icon)
            root.iconbitmap(str(ROOT/'launcher/killzone.ico'))
        except tk.TclError: pass
        root.geometry('1180x860'); root.minsize(1100,800); root.configure(bg=BG)
        root.option_add('*Font', ('Segoe UI',10))
        self.vars = {k:(tk.BooleanVar(value=v) if isinstance(v,bool) else tk.StringVar(value=v))
                     for k,v in self.values.items() if k not in ('keys','pads')}
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TCombobox', fieldbackground=FIELD, background=LINE, foreground=TEXT,
                        arrowcolor=CYAN, bordercolor=LINE, lightcolor=LINE, darkcolor=LINE, padding=5)
        style.map('TCombobox', fieldbackground=[('readonly',FIELD)], foreground=[('readonly',TEXT)])
        style.configure('TCheckbutton', background=PANEL, foreground=TEXT, indicatorbackground=FIELD)
        style.map('TCheckbutton', background=[('active',PANEL)])
        sidebar=tk.Frame(root,bg=BG,width=214,padx=22,pady=28)
        sidebar.pack(side='left',fill='y'); sidebar.pack_propagate(False)
        tk.Label(sidebar,text='KILLZONE',bg=BG,fg=TEXT,font=('Segoe UI',22,'bold'),anchor='w').pack(fill='x')
        tk.Label(sidebar,text='SHADOW FALL',bg=BG,fg=MUTED,font=('Segoe UI',11),anchor='w').pack(fill='x',pady=(0,8))
        tk.Label(sidebar,text='PC EDITION',bg=BG,fg=CYAN,font=('Segoe UI',9,'bold'),anchor='w').pack(fill='x')
        tk.Frame(sidebar,bg=LINE,height=1).pack(fill='x',pady=28)
        self.tab_buttons = {}
        for index,name in enumerate(('Game','Graphics','Controls','Cheats','Advanced'),1):
            b=self.button(sidebar,f'{index:02}   {name}',lambda n=name:self.show_tab(n))
            b.configure(anchor='w',pady=13)
            b.pack(fill='x',pady=4); self.tab_buttons[name]=b
        tk.Label(sidebar,text='Settings apply when\nyou start the game.',bg=BG,fg=MUTED,
                 justify='left',anchor='w',font=('Segoe UI',9)).pack(side='bottom',fill='x',pady=12)
        self.banner=tk.Canvas(sidebar,height=164,bg=BG,highlightthickness=0)
        self.banner.pack(side='bottom',fill='x',pady=16)
        self.banner.bind('<Configure>',self.draw_banner)
        main=tk.Frame(root,bg=PANEL,padx=30,pady=24); main.pack(side='right',fill='both',expand=True)
        top=tk.Frame(main,bg=PANEL); top.pack(fill='x',pady=(0,20))
        self.page_eyebrow=self.label(top,'LAUNCHER',True); self.page_eyebrow.configure(fg=CYAN,font=('Segoe UI',9,'bold')); self.page_eyebrow.pack(fill='x')
        self.page_title=self.label(top,''); self.page_title.configure(font=('Segoe UI',26,'bold')); self.page_title.pack(fill='x',pady=(5,2))
        self.page_description=self.label(top,'',True); self.page_description.pack(fill='x')
        footer=tk.Frame(main,bg=PANEL); footer.pack(side='bottom',fill='x',pady=(18,0))
        tk.Frame(footer,bg=LINE,height=1).pack(fill='x',pady=(0,16))
        actions=tk.Frame(footer,bg=PANEL); actions.pack(fill='x')
        self.session_label=self.label(actions,'Ready to play.',True); self.session_label.configure(wraplength=360)
        self.session_label.pack(side='left',fill='x',expand=True,padx=(0,12))
        self.play_button=self.button(actions,'PLAY GAME',self.play,primary=True); self.play_button.pack(side='right',ipadx=16,ipady=6)
        self.save_button=self.button(actions,'Save settings',self.save); self.save_button.pack(side='right',padx=(0,12))
        self.body=tk.Frame(main,bg=PANEL); self.body.pack(fill='both',expand=True)
        self.pages={}
        for name in self.tab_buttons:
            page=tk.Frame(self.body,bg=PANEL,padx=0,pady=8); self.pages[name]=page
        self.build_game(); self.build_graphics(); self.build_controls(); self.build_cheats(); self.build_advanced()
        self.show_tab('Game'); self.refresh_ready(); self.poll()
        root.protocol('WM_DELETE_WINDOW',self.close)

    def label(self,parent,text,muted=False):
        return tk.Label(parent,text=text,bg=PANEL,fg=MUTED if muted else TEXT,anchor='w',justify='left',wraplength=730)
    def button(self,parent,text,command,primary=False):
        return tk.Button(parent,text=text,command=command,bg=CYAN if primary else FIELD,fg=BG if primary else TEXT,
                         activebackground='#ffab7e' if primary else LINE,activeforeground=BG if primary else TEXT,
                         relief='flat',bd=0,padx=14,pady=8,cursor='hand2')
    def heading(self,parent,text):
        tk.Label(parent,text=text.upper(),bg=PANEL,fg=CYAN,font=('Segoe UI',10,'bold'),anchor='w').pack(fill='x',pady=(0,12))
    def choice(self,parent,label,key,options):
        row=tk.Frame(parent,bg=PANEL); row.pack(fill='x',pady=7)
        self.label(row,label).pack(side='left',fill='x',expand=True)
        labels=OPTION_LABELS.get(key,{str(value):str(value) for value in options})
        display=tk.StringVar(value=labels.get(self.vars[key].get(),self.vars[key].get()))
        box=ttk.Combobox(row,textvariable=display,values=[labels[str(value)] for value in options],state='readonly',width=30)
        reverse={label:value for value,label in labels.items()}
        box.bind('<<ComboboxSelected>>',lambda event:self.vars[key].set(reverse[display.get()]))
        self.vars[key].trace_add('write',lambda *args:display.set(labels.get(self.vars[key].get(),self.vars[key].get())))
        box.pack(side='right'); return box
    def check(self,parent,label,key):
        ttk.Checkbutton(parent,text=label,variable=self.vars[key]).pack(anchor='w',pady=8)
    def show_tab(self,name):
        for page in self.pages.values(): page.pack_forget()
        self.pages[name].pack(fill='both',expand=True)
        for key,b in self.tab_buttons.items(): b.configure(fg=CYAN if key==name else MUTED,bg='#30251f' if key==name else BG)
        titles={'Game':('Game library','Choose your game and manage your saved games.'),
                'Graphics':('Graphics','Display, image quality and frame pacing.'),
                'Controls':('Controls','Tune your controller and customize button mappings.'),
                'Cheats':('Campaign cheats','Optional assists for your campaign.'),
                'Advanced':('Advanced','Additional rendering options and session logs.')}
        title,description=titles[name]
        self.page_title.configure(text=title); self.page_description.configure(text=description)

    def draw_banner(self,event=None):
        c=self.banner; c.delete('all'); w=max(c.winfo_width(),140)
        try:
            from PIL import Image, ImageOps, ImageTk
            with Image.open(ROOT/'launcher/killzone.png') as im:
                im=ImageOps.contain(im.convert('RGBA'),(148,148))
            self.art=ImageTk.PhotoImage(im); c.create_image(w/2,80,image=self.art,anchor='center')
        except (OSError,ImportError):
            c.create_text(w/2,80,text='KZ',fill=CYAN,font=('Segoe UI',36,'bold'))

    def build_game(self):
        p=self.pages['Game']; self.heading(p,'Game library')
        row=tk.Frame(p,bg=PANEL); row.pack(fill='x')
        entry=tk.Entry(row,textvariable=self.vars['game_dir'],bg=FIELD,fg=TEXT,insertbackground=CYAN,relief='flat')
        entry.pack(side='left',fill='x',expand=True,ipady=9)
        self.button(row,'Browse…',self.browse).pack(side='right',padx=(10,0))
        self.ready=self.label(p,''); self.ready.pack(fill='x',pady=(12,24))
        entry.bind('<FocusOut>',lambda e:self.refresh_ready())
        self.choice(p,'Memory profile','profile',PROFILES)
        self.label(p,'Standard uses the default memory layout. Compatibility offers an alternative layout for troubleshooting.',True).pack(fill='x',pady=(5,28))
        self.heading(p,'Folders')
        row=tk.Frame(p,bg=PANEL); row.pack(fill='x')
        for name,path in [('Saved games',ROOT/'user/savedata/1/CUSA00190/KZ4'),('Screenshots',ROOT/'user/screenshots'),('Logs',ROOT/'out')]:
            self.button(row,name,lambda f=path:self.open_folder(f)).pack(side='left',padx=(0,9))
        self.label(p,'Both memory profiles use the same saved games. A separate log is kept for each session.',True).pack(fill='x',pady=15)

    def build_graphics(self):
        p=self.pages['Graphics']; self.heading(p,'Display')
        for label,key,options in [('Display mode','display_mode',('windowed','borderless','fullscreen')),
          ('Window size','window_size',('1280x720','1600x900','1920x1080','2560x1440','3840x2160')),
          ('Frame rate limit','fps',('30','60','120')),('Anti-aliasing','upscaler',('none','fsr3','taa'))]: self.choice(p,label,key,options)
        self.label(p,'Window size controls the display output. The game renders internally at 1920 × 1080.',True).pack(fill='x',pady=12)
        self.check(p,'VSync','vsync'); self.check(p,'Sharpening (RCAS)','rcas'); self.check(p,'Show FPS','show_fps')
        self.check(p,'Motion blur (restart required)','motion_blur')
        self.label(p,'Frame limits do not guarantee performance. FSR 3.1 AA and TAA use native resolution and may introduce trails on moving objects.',True).pack(fill='x',pady=15)

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
        self.heading(p,'Mouse')
        gains=('50','75','100','125','150','175','200','250','300')
        self.check(p,'Enable mouse look','mouse_aim')
        self.choice(p,'Mouse look sensitivity (%)','mouse_sensitivity',gains)
        self.choice(p,'Mouse aim sensitivity / ADS (%)','mouse_ads_sensitivity',('25',)+gains)
        self.choice(p,'Mouse horizontal speed (%)','mouse_horizontal_speed',('25',)+gains)
        self.choice(p,'Mouse vertical speed (%)','mouse_vertical_speed',('25',)+gains)
        self.label(p,'Axis speed multiplies both look and ADS sensitivity. 100% keeps the original mouse scale.',True).pack(fill='x',pady=(0,8))
        self.check(p,'Invert mouse Y axis','mouse_invert_y')
        self.label(p,'Click the game to capture the mouse. Left Alt releases it.\nLeft click fires; right click aims. Insert opens the in-game menu.\nMouse look uses the game\'s camera controls and turn-speed limits.',True).pack(fill='x',pady=(0,20))
        self.heading(p,'Sprint turning · Controller and mouse')
        self.choice(p,'Sprint horizontal turn speed (%)','sprint_turn_speed',('100','125','150','175','200','250','300','400','500'))
        self.label(p,'Independent of horizontal turn speed while sprinting, for controller and mouse. 100% is original; try 200%. Input sensitivity still applies.',True).pack(fill='x',pady=(0,8))
        self.heading(p,'Controller')
        self.choice(p,'Stick drift protection (%)','controller_drift_deadzone',('0','2','4','6','8','10','12','15','20','25','30'))
        self.label(p,'Filters tiny right-stick movements before sensitivity gain. Mouse is unaffected. 0% disables protection.',True).pack(fill='x',pady=(0,8))
        self.choice(p,'Look response curve','look_curve',('0','1','2'))
        self.choice(p,'Look inner deadzone (%)','look_deadzone',('0','2','5','8','10','12','15','20','25','30'))
        self.label(p,'Deadzone applies with custom curves only. These replace native look curves and also affect mouse joystick emulation. Movement is unchanged.',True).pack(fill='x',pady=(0,8))
        self.check(p,'Look ramp smoothing','look_ramp')
        self.check(p,'Aim slowdown / tracking','look_assist')
        self.label(p,'Ramp smoothing controls the native input rise filter. Slowdown/tracking excludes separate zoom-snap and shoot assistance.',True).pack(fill='x',pady=(0,8))
        self.choice(p,'Horizontal turn speed (%)','controller_turn_speed',gains)
        self.choice(p,'Vertical turn speed (%)','controller_pitch_speed',gains)
        self.choice(p,'Look sensitivity (%)','controller_look_gain',gains)
        self.choice(p,'Aim sensitivity / L2 (%)','controller_aim_gain',gains)
        self.label(p,'100% uses the original controls. Turn speed adjusts camera rotation; sensitivity adjusts how quickly the stick reaches full input.',True).pack(fill='x',pady=(0,8))
        self.label(p,'Movement: WASD · Camera: arrow keys · Insert: in-game menu\nSelect a keyboard field and press a key. Esc cancels. Sticks and controller triggers retain their native bindings.',True).pack(fill='x',pady=(0,16))
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
            display=tk.StringVar(value=PAD_LABELS[pv.get()])
            box=ttk.Combobox(table,textvariable=display,values=list(PAD_LABELS.values()),state='readonly',width=17)
            reverse={label:value for value,label in PAD_LABELS.items()}
            box.bind('<<ComboboxSelected>>',lambda event,v=pv,d=display,r=reverse:v.set(r[d.get()]))
            pv.trace_add('write',lambda *args,v=pv,d=display:d.set(PAD_LABELS[v.get()]))
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
        self.vars['mouse_aim'].set(True)
        self.vars['mouse_sensitivity'].set('100')
        self.vars['mouse_ads_sensitivity'].set('100')
        self.vars['mouse_horizontal_speed'].set('100')
        self.vars['mouse_vertical_speed'].set('100')
        self.vars['mouse_invert_y'].set(False)
        self.vars['controller_turn_speed'].set('100')
        self.vars['sprint_turn_speed'].set('100')
        self.vars['look_curve'].set('1');self.vars['look_deadzone'].set('8')
        self.vars['look_ramp'].set(False);self.vars['look_assist'].set(True)
        self.vars['controller_drift_deadzone'].set('8')
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
        self.label(p,'Keep ammunition available and fire without reloading.',True).pack(fill='x',pady=(0,20))
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
        ok,text=validate_game(self.vars['game_dir'].get()); self.ready.configure(text=text,fg=GREEN if ok else CYAN)
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
            if not path.is_file(): messagebox.showerror('Setup incomplete','A required game component is missing. Complete the build and game preparation steps in the README before playing.',parent=self.root); return
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
            self.launched_values=values
            self.play_button.configure(text='GAME RUNNING',state='disabled'); self.session_label.configure(text='Game running. Enjoy your session.',fg=CYAN)
        except (OSError,ValueError) as exc: messagebox.showerror('Launch failed',str(exc),parent=self.root)
    def poll(self):
        if self.monitor and self.monitor.poll() is not None:
            updated=load_preferences()
            for key in LIVE_CONFIG:
                # Preserve edits made in the launcher during this game session.
                if self.vars[key].get()==self.launched_values[key]:self.vars[key].set(updated[key])
            try:
                result=json.loads((self.session/'status.json').read_text())
                text='Game closed. Ready to play again.' if result.get('exit_code')==0 else 'Game stopped unexpectedly. See session logs.'
            except (OSError,ValueError): text='Game session ended. See session logs.'
            self.session_label.configure(text=text,fg=MUTED); self.play_button.configure(text='PLAY GAME',state='normal'); self.monitor=None
        self.root.after(1000,self.poll)
    def close(self):
        try: persist(self.gather())
        except OSError: pass
        self.root.destroy()

def configure_windows_identity():
    """Give the Tk window its own taskbar group instead of Python's identity."""
    if os.name!='nt':
        return
    import ctypes
    app_id=ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID
    app_id.argtypes=[ctypes.c_wchar_p]
    app_id.restype=ctypes.c_long
    app_id('pearlxcore.KillzoneShadowFall.Launcher')


def main():
    if len(sys.argv)>2 and sys.argv[1]=='--run-session':
        # Child monitor inherits the launch environment, not a copy written to disk.
        run_session(sys.argv[2]); return
    configure_windows_identity()
    if os.name=='nt':
        import ctypes
        try: ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except OSError: pass
    root=tk.Tk(); Launcher(root); root.mainloop()

if __name__=='__main__': main()
