#!/usr/bin/env python3
"""Killzone Shadow Fall launcher and independent game-session monitor."""
from __future__ import annotations
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import tempfile
import webbrowser
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER_VERSION = '0.2.0'
KOFI_URL = 'https://ko-fi.com/R6R524N7X'
sys.path.insert(0, str(ROOT / 'scripts'))
from graphics_options import read_ini, write_keys
from prepare import read_sfo
from kzsf_catalog import load_catalog, verify_game_build

PREFS = ROOT / 'user' / 'launcher-local.json'
CONFIG = ROOT / 'user' / 'launcher-settings.ini'
BG, PANEL, FIELD, LINE = '#080f17', '#0e1823', '#172633', '#273b4a'
TEXT, MUTED, CYAN, GREEN = '#e5eef5', '#91a7b8', '#69d4e3', '#9bd0aa'
PRIMARY = '#f4a66a'
OPTION_LABELS = {
    'prompt_style': {'0':'Automatic', '1':'Keyboard & mouse', '2':'PlayStation'},
    'look_curve': {'0':'Original', '1':'Linear (custom)', '2':'Precision (custom)'},
    'profile': {'Current build': 'Standard', 'Beta memory layout (test)': 'Compatibility'},
    'upscaler': {'none': 'Native', 'fsr3': 'FSR 3.1 AA', 'taa': 'TAA'},
    'display_mode': {'windowed': 'Windowed', 'borderless': 'Borderless', 'fullscreen': 'Fullscreen'},
    'frames_queued': {'0': 'Automatic', '1': '1 frame', '2': '2 frames'},
    'frame_gen': {'off':'Off', 'fsr3':'FSR 3.1 — 2x'},
    'render_output': {'1920x1080':'1080p (1920 × 1080)', '2560x1440':'1440p (2560 × 1440)',
                      '3200x1800':'1800p (3200 × 1800)', '3840x2160':'4K (3840 × 2160)'},
    'anisotropy': {'0':'Game default', '2':'2x', '4':'4x', '8':'8x', '16':'16x'},
    'mip_lod_bias': {'-2':'Sharper (-2)', '-1':'Sharper (-1)', '-0.5':'Slightly sharper (-0.5)',
                     '0':'Game default', '0.5':'Slightly softer (+0.5)', '1':'Softer (+1)', '2':'Softer (+2)'},
}
RENDER_OUTPUTS = ('1920x1080','2560x1440','3200x1800','3840x2160')
GRAPHICS_EXE = 'out/gow3-probe-launcher.exe'
PROFILES = {'Current build': 'out/gow3-probe-launcher.exe',
            'Beta memory layout (test)': 'out/gow3-probe-launcher.exe'}
BUTTONS = [('cross','Cross / Jump','Space','a'), ('circle','Circle','Left Ctrl','b'),
           ('square','Square','E','x'), ('triangle','Triangle','Q','y'),
           ('l1','L1','1','leftshoulder'), ('r1','R1','3','rightshoulder'),
           ('l2','L2','R',''), ('r2','R2','F',''), ('l3','L3','Left Shift','leftstick'),
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
DEFAULTS = dict(game_dir='', cache_dir='', savedata_dir='', profile='Current build',
                fps='60', window_size='1920x1080', display_mode='windowed', vsync=True,
                rcas=True, show_fps=True, upscaler='none', async_shaders=False,
                stale_readback=True, deferred_readback=True, frames_queued='1',
                unlimited_health=False, infinite_ammo=False,
                controller_look_gain='100', controller_aim_gain='100', controller_turn_speed='100',
                controller_pitch_speed='100', keys={}, pads={})
DEFAULTS.update(mouse_aim=True,mouse_sensitivity='100',mouse_ads_sensitivity='100',mouse_invert_y=False)
DEFAULTS.update(motion_blur=True,bloom=True,film_grain=True)
DEFAULTS.update(fov_percent='100',camera_shake=True,depth_of_field=True)
DEFAULTS.update(mouse_horizontal_speed='100',mouse_vertical_speed='100')
DEFAULTS.update(sprint_turn_speed='100')
DEFAULTS.update(look_curve='1',look_deadzone='8',look_ramp=False,look_assist=True)
DEFAULTS.update(controller_drift_deadzone='8')
DEFAULTS.update(prompt_style='0')
DEFAULTS.update(render_output='1920x1080',frame_gen='off',anisotropy='0',mip_lod_bias='0',
                fsr1=False,rcas_strength='75')
DEFAULTS.update(third_person=False,free_camera=False,model_switching=False)
DEFAULTS.update(unlimited_sprint=False,continuous_scan=False)
PLAYER_HOOK_ENV = (
    'KZSF_CAMERA_OBSERVER','KZSF_CAMERA_CALL_BRIDGE','KZSF_THIRD_PERSON_TEST',
    'KZSF_FOLLOW_FREE_CAMERA','KZSF_FREE_CAMERA_TEST','KZSF_FREE_CAMERA_BODY',
    'KZSF_CHARACTER_SWAP_TEST','KZSF_CHARACTER_SELECTOR_TEST','KZSF_CHARACTER_RESOURCE_TEST',
    'KZSF_CHARACTER_RETARGET_AUDIT','KZSF_CHARACTER_ENGINEER_TEST','KZSF_CHARACTER_ENGINEER_DRAW_TEST',
    'KZSF_CHARACTER_BODY_ROUTE_TEST','KZSF_CHARACTER_SINGLE_BODY_TEST','KZSF_CHARACTER_COMMANDO_TEST',
    'KZSF_CHARACTER_OVERLAY_TEST','KZSF_CHARACTER_ALL_MODELS_TEST','KZSF_CHARACTER_BUNDLE_TEST',
    'KZSF_CHARACTER_PRESERVE_SOURCE_BODY','KZSF_CHARACTER_FACE_PLAYER_ANIMATION_CONTROL')
LIVE_CONFIG = {'fps':'fps_limit', 'upscaler':'upscaler', 'display_mode':'display_mode',
    'render_output':'output_res',
    **{key:key for key in ('frame_gen','anisotropy','mip_lod_bias','fsr1','rcas_strength')},
    **{key:key for key in ('vsync','rcas','show_fps','async_shaders','stale_readback',
       'deferred_readback','frames_queued','prompt_style','mouse_aim','mouse_invert_y','mouse_sensitivity',
       'mouse_ads_sensitivity','mouse_horizontal_speed','mouse_vertical_speed','motion_blur','bloom','film_grain',
       'fov_percent','camera_shake','depth_of_field',
       'unlimited_health','infinite_ammo','unlimited_sprint','continuous_scan','controller_look_gain','controller_aim_gain',
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
    for key in ('cache_dir','savedata_dir'):
        if not isinstance(values[key],str): values[key]=''
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
                         ('upscaler', ('none','fsr3','taa')), ('frames_queued', ('1','2','0')),
                         ('render_output', RENDER_OUTPUTS), ('frame_gen', ('off','fsr3')),
                         ('anisotropy', ('0','2','4','8','16')),
                         ('prompt_style', ('0','1','2'))]:
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
    fov=str(values.get('fov_percent','100'))
    values['fov_percent']=fov if fov.isdigit() and 75<=int(fov)<=150 else '100'
    if str(values['look_curve']) not in ('0','1','2'):values['look_curve']='0'
    deadzone=str(values['look_deadzone'])
    values['look_deadzone']=deadzone if deadzone.isdigit() and 0<=int(deadzone)<=30 else '8'
    drift=str(values['controller_drift_deadzone'])
    values['controller_drift_deadzone']=drift if drift.isdigit() and 0<=int(drift)<=30 else '8'
    try:
        bias=float(values['mip_lod_bias'])
        values['mip_lod_bias']=format(round(bias*8)/8,'g') if -2<=bias<=2 else '0'
    except (ValueError,TypeError): values['mip_lod_bias']='0'
    strength=str(values['rcas_strength'])
    values['rcas_strength']=strength if strength.isdigit() and 0<=int(strength)<=100 else '75'
    return values

def validate_game(folder):
    folder = Path(folder)
    if not (folder / 'eboot.bin').is_file(): return False, 'Choose the game folder containing eboot.bin.'
    try:
        info = read_sfo(folder / 'sce_sys' / 'param.sfo') or {}
        title, version = info.get('TITLE_ID',''), info.get('APP_VER','')
        if title != 'CUSA00190' or version != '01.81':
            return False, f'Expected CUSA00190 / 01.81; found {title or "unknown title"} / {version or "unknown version"}.'
        verify_game_build(folder)
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
    for key in ('prompt_style','mouse_aim','mouse_invert_y','mouse_sensitivity','mouse_ads_sensitivity',
                'mouse_horizontal_speed','mouse_vertical_speed','motion_blur','bloom','film_grain','unlimited_health','infinite_ammo',
                'fov_percent','camera_shake','depth_of_field','unlimited_sprint','continuous_scan',
                'look_curve','look_deadzone','look_ramp','look_assist','controller_drift_deadzone'):
        value=values.get(key,DEFAULTS[key])
        updates[key]=str(int(value)) if isinstance(value,bool) else str(value)
    write_keys(CONFIG, updates)
    write_keys(CONFIG, {LIVE_CONFIG[k]:str(int(values.get(k,DEFAULTS[k]))) if isinstance(DEFAULTS[k],bool)
                       else str(values.get(k,DEFAULTS[k]))
                       for k in ('render_output','frame_gen','anisotropy','mip_lod_bias','fsr1','rcas_strength')})

def executable_for(values):
    if any(values.get(k,False) for k in ('third_person','free_camera','model_switching','unlimited_sprint','continuous_scan')):
        return GRAPHICS_EXE
    # Route the new settings to a separate candidate until gameplay validation is complete.
    # Returning these choices to their defaults restores the previously deployed executable.
    if any(values.get(k,DEFAULTS[k]) != DEFAULTS[k] for k in
           ('render_output','frame_gen','anisotropy','mip_lod_bias','fsr1','rcas_strength')):
        return GRAPHICS_EXE
    # Features are opt-in flags in one current build, so changing assists or
    # controls cannot silently select an older overlay/renderer executable.
    return PROFILES[values['profile']]


def needs_preparation(game_dir):
    if any(not (ROOT/'out'/name).is_file() for name in ('boot-linked.bin','content.bin')):
        return True
    stamp = ROOT/'out/prepared-game.json'
    if not stamp.exists():
        return True  # Verify the supported assets before accepting old prepared images.
    try:
        prepared = json.loads(stamp.read_text(encoding='utf-8'))
        return (prepared.get('catalog_id') != load_catalog()['catalog_id'] or
                Path(prepared['game_dir']).resolve() != Path(game_dir).resolve())
    except (OSError, ValueError, KeyError, TypeError):
        return True

def storage_directory(values, key):
    """Resolve optional storage roots without changing unrelated user files."""
    value=values.get(key,'').strip()
    if not value: return ROOT/'user'/('cache' if key=='cache_dir' else 'savedata')
    path=Path(os.path.expandvars(value)).expanduser()
    return (path if path.is_absolute() else ROOT/path).resolve()


def prepare_storage(values):
    for key in ('cache_dir','savedata_dir'):
        path=storage_directory(values,key)
        # Save mounts use a fixed-size native path buffer, including slot names.
        if key=='savedata_dir' and len(path.as_posix().encode('utf-8'))>350:
            raise ValueError('Choose a shorter saved-data folder path (350 bytes or fewer).')
        path.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryFile(dir=path): pass


def launch_spec(values, folder):
    env = dict(os.environ)
    for key,variable in (('cache_dir','GOW3_CACHE_DIR'),('savedata_dir','GOW3_SAVEDATA_DIR')):
        env.pop(variable,None)
        if values.get(key,'').strip(): env[variable]=storage_directory(values,key).as_posix()
    for key in PLAYER_HOOK_ENV:
        env.pop(key,None)
    for key in ('KZSF_ORIGINAL_CLOCK','KZSF_UNIFIED_TSC','KZSF_FILE_TRACE','KZSF_RWLOCK_TRACE',
                'GOW3_TRACE_IMPORTS','GOW3_TSC_HZ','VK_INSTANCE_LAYERS','VK_LOADER_LAYERS_ENABLE',
                'GOW3_PAD_FILE','GOW3_NO_MOVIES','GOW3_AUDIO','GOW3_AUDIO_DUMP','GOW3_WRITE_LOG',
                'GOW3_USER_DIR','GOW3_GPU_USER_DIR','GOW3_DATA_DIR','GOW3_FREEZE_EXIT','KZSF_ENGINE_TURN',
                'KZSF_TURN_LOG','KZSF_PLAYER_TURN','KZSF_FSR_NATIVE','KZSF_FSR_QUALITY','GOW3_RENDER_RES',
                'GOW3_SCENE_NATIVE','GOW3_NATIVE_AA','GOW3_FRAME_GEN','GOW3_GAME_TAA_PS',
                'GOW3_UI_TRIGGER_VS','GOW3_KZ_VELOCITY','GOW3_DISPLAY_BLIT',
                'GOW3_PRESET_FILE','GOW3_SCENE_DEBUG','KZSF_DIRECT_TEXTURE_MEMO','KZSF_FREE_CAMERA_TEST',
                'KZSF_FREE_CAMERA_BODY','KZSF_CAMERA_OBSERVER','KZSF_CAMERA_CALL_BRIDGE','KZSF_THIRD_PERSON_TEST','KZSF_THIRD_PERSON_AUTOTEST'):
        env.pop(key, None)
    beta = values['profile'] == 'Beta memory layout (test)'
    env.update(PATH=r'C:\msys64\clang64\bin'+';'+env.get('PATH',''),
        KZSF_VIRTUAL_TSC='1', KZSF_BETA_MEMORY_LAYOUT='1' if beta else '0',
        KZSF_UNLIMITED_HEALTH=str(int(values.get('unlimited_health',False))),
        KZSF_INFINITE_AMMO=str(int(values.get('infinite_ammo',False))),
        KZSF_UNLIMITED_SPRINT=str(int(values.get('unlimited_sprint',False))),
        KZSF_CONTINUOUS_SCAN=str(int(values.get('continuous_scan',False))),
        KZSF_DISABLE_MOTION_BLUR=str(int(not values.get('motion_blur',True))),
        KZSF_DISABLE_DOF=str(int(not values.get('depth_of_field',True))),
        KZSF_DISABLE_BLOOM=str(int(not values.get('bloom',True))),
        KZSF_DISABLE_FILM_GRAIN=str(int(not values.get('film_grain',True))),
        KZSF_CAMERA_CONTROLS='1',KZSF_FOV_PERCENT=values.get('fov_percent','100'),
        KZSF_CAMERA_SHAKE=str(int(values.get('camera_shake',True))),
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
        KZSF_NATIVE_PROMPTS='1', KZSF_PROMPT_STYLE=str(values.get('prompt_style','0')),
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
    keyboard_defaults={key:keyboard for key,label,keyboard,pad in BUTTONS}
    keyboard_defaults.update(values['keys'])
    env['GOW3_KEY_MAP'] = ','.join(f'{k}={v}' for k,v in keyboard_defaults.items() if v)
    env['GOW3_PAD_MAP'] = ','.join(f'{k}={v}' for k,v in values['pads'].items() if v)
    executable=executable_for(values)
    env.update(KZSF_DIRECT_TSC='1',GOW3_PRECOMPILE='1',GOW3_BACKGROUND_WARMUP='0',GOW3_FREEZE_SECONDS='0')
    # Preserve a serial diagnostic override; normal startup uses the existing
    # bounded compiler pool and still waits for every cached pipeline.
    env['GOW3_PARALLEL_WARMUP']='0' if env.get('GOW3_PARALLEL_WARMUP')=='0' else '1'
    # Keep the guarded player hook available for live overlay adjustments from 100%.
    env['KZSF_PLAYER_TURN']='1'
    if values['upscaler'] in ('fsr3','taa'):
        env.update(KZSF_FSR_NATIVE='1',GOW3_UPSCALE_PRESET='0',GOW3_OUTPUT_RES='1920x1080',
                   GOW3_JITTER='0',GOW3_REACTIVE='0',GOW3_OBJECT_MOTION='0')
    output=values.get('render_output',DEFAULTS['render_output'])
    fg=values.get('frame_gen','off')
    env.update(GOW3_FRAME_GEN=fg,GOW3_OUTPUT_RES=output,
               GOW3_ANISOTROPY=values.get('anisotropy','0'),
               GOW3_MIP_LOD_BIAS=values.get('mip_lod_bias','0'),
               GOW3_FSR1=str(int(values.get('fsr1',False))),
               GOW3_RCAS_STRENGTH=values.get('rcas_strength','75'))
    # Keep the stable native/AA paths at 1080p. The output reconstruction path retains
    # all guest scene targets at 1080p and draws HUD/menu pixels at the selected output.
    if output!='1920x1080' or fg!='off':
        env.update(GOW3_UPSCALER='fsr3',KZSF_FSR_NATIVE='1',GOW3_SCENE_NATIVE='1',
                   GOW3_NATIVE_AA='1' if output=='1920x1080' else '0',
                   GOW3_UPSCALE_PRESET='0',GOW3_UI_TRIGGER_VS='6a09714a',
                   GOW3_JITTER='0',GOW3_REACTIVE='0',GOW3_OBJECT_MOTION='0')
    # Freecam and character rendering share the validated follow-camera bridge.
    # Checkboxes enable controls; camera mode/model selection stays an in-game action.
    if any(values.get(k,False) for k in ('third_person','free_camera','model_switching')):
        env.update(KZSF_CAMERA_OBSERVER='1',KZSF_CAMERA_CALL_BRIDGE='1',KZSF_THIRD_PERSON_TEST='1')
    if values.get('free_camera',False):
        env['KZSF_FOLLOW_FREE_CAMERA']='1'
    if values.get('model_switching',False):
        env.update(KZSF_CHARACTER_SWAP_TEST='1',KZSF_CHARACTER_SELECTOR_TEST='1',
                   KZSF_CHARACTER_ENGINEER_TEST='1',KZSF_CHARACTER_ENGINEER_DRAW_TEST='1',
                   KZSF_CHARACTER_BODY_ROUTE_TEST='1',KZSF_CHARACTER_SINGLE_BODY_TEST='1',
                   KZSF_CHARACTER_OVERLAY_TEST='1',KZSF_CHARACTER_ALL_MODELS_TEST='1',
                   KZSF_CHARACTER_BUNDLE_TEST='1',KZSF_CHARACTER_PRESERVE_SOURCE_BODY='0',
                   KZSF_CHARACTER_FACE_PLAYER_ANIMATION_CONTROL='0')
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
        self.preparing = None
        self.session = None
        root.title(f'Killzone Shadow Fall — v{LAUNCHER_VERSION}')
        try:
            self.icon=tk.PhotoImage(file=str(ROOT/'launcher/killzone.png'))
            root.iconphoto(True,self.icon)
            root.iconbitmap(str(ROOT/'launcher/killzone.ico'))
        except tk.TclError: pass
        root.geometry('1240x900'); root.minsize(1100,740); root.configure(bg=BG)
        root.option_add('*Font', ('Segoe UI',10))
        self.vars = {k:(tk.BooleanVar(value=v) if isinstance(v,bool) else tk.StringVar(value=v))
                     for k,v in self.values.items() if k not in ('keys','pads')}
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TCombobox', fieldbackground=FIELD, background=LINE, foreground=TEXT,
                        arrowcolor=CYAN, bordercolor=LINE, lightcolor=LINE, darkcolor=LINE, padding=5)
        style.map('TCombobox', fieldbackground=[('readonly',FIELD)], foreground=[('readonly',TEXT)])
        style.configure('TCheckbutton', background=PANEL, foreground=TEXT, indicatorbackground=FIELD,padding=(0,3))
        style.map('TCheckbutton', background=[('active',PANEL)],foreground=[('disabled',MUTED)],
                  indicatorbackground=[('selected',CYAN),('active',LINE)])
        style.configure('Vertical.TScrollbar',background=LINE,troughcolor=PANEL,bordercolor=PANEL,
                        arrowcolor=MUTED,lightcolor=LINE,darkcolor=LINE,width=12)
        mast=tk.Frame(root,bg=BG,padx=32,pady=20); mast.pack(fill='x')
        identity=tk.Frame(mast,bg=BG);identity.pack(side='left')
        tk.Frame(identity,bg=PRIMARY,width=4,height=36).pack(side='left',padx=(0,14))
        tk.Label(identity,text='KILLZONE',bg=BG,fg=TEXT,font=('Segoe UI',22,'bold')).pack(side='left')
        tk.Label(identity,text=' / ',bg=BG,fg=LINE,font=('Segoe UI',22)).pack(side='left',padx=8)
        tk.Label(identity,text='SHADOW FALL',bg=BG,fg=MUTED,font=('Segoe UI',12)).pack(side='left')
        self.support_button=self.button(mast,'Support me on Ko-fi',lambda:webbrowser.open_new_tab(KOFI_URL))
        try:
            self.support_badge=tk.PhotoImage(file=str(ROOT/'launcher/kofi.png'))
            self.support_button.configure(image=self.support_badge,padx=0,pady=0,bg=BG,activebackground=BG)
        except tk.TclError: pass
        self.support_button.pack(side='right')
        navigation=tk.Frame(root,bg=BG,padx=32);navigation.pack(fill='x')
        self.tab_buttons = {}
        self.tab_indicators={}
        for name in ('Game','Graphics','Controls','Cheats','Advanced'):
            tab=tk.Frame(navigation,bg=BG);tab.pack(side='left',padx=(0,6))
            b=self.button(tab,'HOME' if name=='Game' else name.upper(),lambda n=name:self.show_tab(n))
            b.configure(bg=BG,pady=12,padx=20,font=('Segoe UI',10,'bold'))
            b.pack(fill='x'); self.tab_buttons[name]=b
            rule=tk.Frame(tab,bg=BG,height=3);rule.pack(fill='x');self.tab_indicators[name]=rule
        tk.Frame(root,bg=LINE,height=1).pack(fill='x')
        footer=tk.Frame(root,bg=BG,padx=32,pady=18);footer.pack(side='bottom',fill='x')
        actions=tk.Frame(footer,bg=BG); actions.pack(fill='x')
        status=tk.Frame(actions,bg=BG);status.pack(side='left',fill='x',expand=True,padx=(0,12))
        self.session_label=self.label(status,'Ready to play.',True); self.session_label.configure(wraplength=360)
        self.session_label.configure(bg=BG,wraplength=530)
        self.session_label.pack(anchor='w')
        tk.Label(status,text='Settings apply when you start the game.',bg=BG,fg=MUTED,font=('Segoe UI',9)).pack(anchor='w',pady=(3,0))
        launch_actions=tk.Frame(actions,bg=BG);launch_actions.pack(side='right')
        for column in (0,2):launch_actions.columnconfigure(column,weight=1,uniform='launch-buttons')
        launch_actions.columnconfigure(1,minsize=12)
        self.play_button=self.button(launch_actions,'PLAY GAME',self.play,primary=True)
        self.save_button=self.button(launch_actions,'Save settings',self.save)
        for button,column in ((self.save_button,0),(self.play_button,2)):
            button.configure(width=18,font=('Segoe UI',10,'bold'))
            button.grid(row=0,column=column,sticky='nsew',ipady=6)
        tk.Frame(root,bg=LINE,height=1).pack(side='bottom',fill='x')
        main=tk.Frame(root,bg=BG,padx=32,pady=22);main.pack(fill='both',expand=True)
        top=tk.Frame(main,bg=BG); top.pack(fill='x',pady=(0,18))
        self.page_title=self.label(top,''); self.page_title.configure(bg=BG,font=('Segoe UI',24,'bold')); self.page_title.pack(anchor='w')
        self.page_description=self.label(top,'',True);self.page_description.configure(bg=BG); self.page_description.pack(anchor='w',pady=(4,0))
        self.body=tk.Frame(main,bg=PANEL); self.body.pack(fill='both',expand=True)
        self.pages={}
        for name in self.tab_buttons:
            page=tk.Frame(self.body,bg=PANEL,padx=20,pady=18); self.pages[name]=page
        self.build_game(); self.build_graphics(); self.build_controls(); self.build_cheats(); self.build_advanced()
        self.show_tab('Game'); self.refresh_ready(); self.poll()
        root.protocol('WM_DELETE_WINDOW',self.close)
        for index,name in enumerate(self.pages,1):
            root.bind(f'<Control-Key-{index}>',lambda event,n=name:self.show_tab(n))

    def label(self,parent,text,muted=False):
        return tk.Label(parent,text=text,bg=PANEL,fg=MUTED if muted else TEXT,anchor='w',justify='left',wraplength=730)
    def button(self,parent,text,command,primary=False):
        return tk.Button(parent,text=text,command=command,bg=PRIMARY if primary else FIELD,fg=BG if primary else TEXT,
                         activebackground='#ffc08d' if primary else LINE,activeforeground=BG if primary else TEXT,
                         disabledforeground=MUTED,font=('Segoe UI',10,'bold' if primary else 'normal'),
                         highlightthickness=1,highlightbackground=LINE,highlightcolor=CYAN,
                         relief='flat',bd=0,padx=16,pady=9,cursor='hand2')
    def heading(self,parent,text):
        bar=tk.Frame(parent,bg=PANEL);bar.pack(fill='x',pady=(8,12))
        tk.Label(bar,text=text.upper(),bg=PANEL,fg=CYAN,font=('Segoe UI',9,'bold'),anchor='w').pack(side='left',padx=(0,16))
        tk.Frame(bar,bg=LINE,height=1).pack(side='left',fill='x',expand=True)
    def choice(self,parent,label,key,options):
        row=tk.Frame(parent,bg=FIELD,padx=14,pady=8); row.pack(fill='x',pady=3)
        caption=self.label(row,label);caption.configure(bg=FIELD);caption.pack(side='left',fill='x',expand=True)
        labels=OPTION_LABELS.get(key,{str(value):str(value) for value in options})
        display=tk.StringVar(value=labels.get(self.vars[key].get(),self.vars[key].get()))
        box=ttk.Combobox(row,textvariable=display,values=[labels[str(value)] for value in options],state='readonly',width=30)
        reverse={label:value for value,label in labels.items()}
        box.bind('<<ComboboxSelected>>',lambda event:self.vars[key].set(reverse[display.get()]))
        self.vars[key].trace_add('write',lambda *args:display.set(labels.get(self.vars[key].get(),self.vars[key].get())))
        box.pack(side='right'); return box
    def check(self,parent,label,key,command=None):
        ttk.Checkbutton(parent,text=label,variable=self.vars[key],command=command).pack(anchor='w',pady=8)
    def scroll_content(self,page):
        canvas=tk.Canvas(page,bg=PANEL,highlightthickness=0)
        scroll=ttk.Scrollbar(page,orient='vertical',command=canvas.yview)
        scroll.pack(side='right',fill='y');canvas.pack(side='left',fill='both',expand=True)
        canvas.configure(yscrollcommand=scroll.set)
        content=tk.Frame(canvas,bg=PANEL)
        window=canvas.create_window((0,0),window=content,anchor='nw')
        content.bind('<Configure>',lambda event:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda event:canvas.itemconfigure(window,width=event.width))
        def wheel(event):
            if canvas.winfo_ismapped() and event.widget.winfo_class()!='TCombobox':
                canvas.yview_scroll(-int(event.delta/120),'units')
        self.root.bind('<MouseWheel>',wheel,add='+')
        return content
    def show_tab(self,name):
        for page in self.pages.values(): page.pack_forget()
        self.pages[name].pack(fill='both',expand=True)
        for key,b in self.tab_buttons.items():
            b.configure(fg=TEXT if key==name else MUTED,bg=FIELD if key==name else BG)
            self.tab_indicators[key].configure(bg=CYAN if key==name else BG)
        titles={'Game':('Your game','Set up your game, choose your perspective, and play.'),
                'Graphics':('Graphics','Display, image quality and frame pacing.'),
                'Controls':('Controls','Tune your controller and customize button mappings.'),
                'Cheats':('Cheats','Optional assists for your game.'),
                'Advanced':('Advanced','Additional rendering options and session logs.')}
        title,description=titles[name]
        self.page_title.configure(text=title); self.page_description.configure(text=description)

    def build_game(self):
        page=self.pages['Game']
        canvas=tk.Canvas(page,bg=PANEL,highlightthickness=0,height=430)
        scroll=ttk.Scrollbar(page,orient='vertical',command=canvas.yview)
        scroll.pack(side='right',fill='y');canvas.pack(side='left',fill='both',expand=True)
        canvas.configure(yscrollcommand=scroll.set)
        p=tk.Frame(canvas,bg=PANEL)
        window=canvas.create_window((0,0),window=p,anchor='nw')
        p.bind('<Configure>',lambda event:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda event:canvas.itemconfigure(window,width=event.width))
        def wheel(event):
            if canvas.winfo_ismapped() and event.widget.winfo_class()!='TCombobox':
                canvas.yview_scroll(-int(event.delta/120),'units')
        self.root.bind('<MouseWheel>',wheel,add='+')
        self.heading(p,'Game library')
        row=tk.Frame(p,bg=PANEL); row.pack(fill='x')
        entry=tk.Entry(row,textvariable=self.vars['game_dir'],bg=FIELD,fg=TEXT,insertbackground=CYAN,relief='flat')
        entry.pack(side='left',fill='x',expand=True,ipady=9)
        self.button(row,'Browse…',self.browse).pack(side='right',padx=(10,0))
        self.ready=self.label(p,''); self.ready.pack(fill='x',pady=(12,24))
        entry.bind('<FocusOut>',lambda e:self.refresh_ready())
        self.choice(p,'Memory profile','profile',PROFILES)
        self.label(p,'Standard uses the default memory layout. Compatibility offers an alternative layout for troubleshooting.',True).pack(fill='x',pady=(5,28))
        self.heading(p,'Folders')
        for title,key in (('Shader cache folder','cache_dir'),('Saved-data folder','savedata_dir')):
            self.label(p,title).pack(anchor='w',pady=(0,6))
            row=tk.Frame(p,bg=PANEL); row.pack(fill='x',pady=(0,8))
            tk.Entry(row,textvariable=self.vars[key],bg=FIELD,fg=TEXT,insertbackground=CYAN,
                     relief='flat').pack(side='left',fill='x',expand=True,ipady=9)
            self.button(row,'Browse…',lambda k=key:self.browse_storage(k)).pack(side='right',padx=(10,0))
            self.label(p,f'Empty uses: {storage_directory(DEFAULTS,key)}',True).pack(fill='x',pady=(0,12))
        self.label(p,'Choose a root folder; game-specific subfolders are added automatically. Changing folders does not move existing files. Changes apply on the next launch.',True).pack(fill='x',pady=(0,14))
        row=tk.Frame(p,bg=PANEL); row.pack(fill='x')
        self.button(row,'Saved games',lambda:self.open_folder(storage_directory(self.gather(),'savedata_dir')/'1/CUSA00190/KZ4')).pack(side='left',padx=(0,9))
        self.button(row,'Shader cache',lambda:self.open_folder(storage_directory(self.gather(),'cache_dir')/'CUSA00190')).pack(side='left',padx=(0,9))
        for name,path in [('Screenshots',ROOT/'user/screenshots'),('Logs',ROOT/'out')]:
            self.button(row,name,lambda f=path:self.open_folder(f)).pack(side='left',padx=(0,9))
        self.label(p,'Both memory profiles use the same saved games. A separate log is kept for each session.',True).pack(fill='x',pady=15)
        self.heading(p,'Camera & player model')
        self.check(p,'Enable third-person controls · F8','third_person',lambda:self.player_feature_changed('third_person'))
        self.check(p,'Enable freecam controls · F9','free_camera',lambda:self.player_feature_changed('free_camera'))
        self.check(p,'Enable model switching · F7 / overlay','model_switching',lambda:self.player_feature_changed('model_switching'))
        self.label(p,'Freecam and model switching also enable third-person controls. Changes apply on the next launch. F8 toggles third person; F9 toggles freecam. Select a model with Insert → Game → Player model, or hold F7 + Left/Right. F7 alone restores/toggles the selected model.',True).pack(fill='x',pady=(4,8))
        self.label(p,'Freecam: WASD move, Q/E down/up, mouse/arrows look, Shift faster, Ctrl slower, F10 recenter. Check camera and model behavior when combining these options with frame generation.',True).pack(fill='x',pady=(0,8))

    def player_feature_changed(self,key):
        if key=='third_person' and not self.vars[key].get():
            self.vars['free_camera'].set(False)
            self.vars['model_switching'].set(False)
        elif self.vars[key].get():
            self.vars['third_person'].set(True)

    def build_graphics(self):
        page=self.pages['Graphics']
        canvas=tk.Canvas(page,bg=PANEL,highlightthickness=0)
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
        self.heading(p,'Display')
        for label,key,options in [('Display mode','display_mode',('windowed','borderless','fullscreen')),
          ('Window size','window_size',('1280x720','1600x900','1920x1080','2560x1440','3840x2160')),
          ('Render resolution (FSR output)','render_output',RENDER_OUTPUTS),
          ('Frame generation','frame_gen',('off','fsr3')),
          ('Anisotropic filtering','anisotropy',('0','2','4','8','16')),
          ('Distant texture sharpness','mip_lod_bias',tuple(OPTION_LABELS['mip_lod_bias'])),
          ('Frame rate limit','fps',('30','60','120')),('Anti-aliasing (1080p, FG off)','upscaler',('none','fsr3','taa'))]: self.choice(p,label,key,options)
        self.label(p,'Scene rendering stays at 1920 × 1080. Higher render resolutions use FSR 3.1 with the HUD drawn at that size. Window size is independent. Higher resolution or frame generation selects the FSR path regardless of the anti-aliasing choice. Changes apply on the next launch.',True).pack(fill='x',pady=12)
        self.label(p,'Frame generation inserts an intermediate frame; it does not double game simulation FPS. Check for visual artifacts when enabling it. Texture sharpness affects distant material detail; sharper values may shimmer.',True).pack(fill='x',pady=8)
        self.check(p,'FSR 1 spatial upscaling (when the window is larger than the game image)','fsr1')
        self.check(p,'VSync','vsync'); self.check(p,'Sharpening (RCAS)','rcas'); self.check(p,'Show FPS','show_fps')
        self.choice(p,'RCAS sharpening strength (%)','rcas_strength',('0','25','50','75','100'))
        self.check(p,'Motion blur (restart required)','motion_blur')
        self.choice(p,'Field of view (%)','fov_percent',('75','90','100','110','120','130','140','150'))
        self.check(p,'Camera shake','camera_shake')
        self.check(p,'Depth of field (restart required)','depth_of_field')
        self.check(p,'Bloom (restart required)','bloom')
        self.check(p,'Film grain (restart required)','film_grain')
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
        self.choice(p,'Button prompts','prompt_style',('0','1','2'))
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
        def arrange_bindings(event):
            columns=2 if event.width>=1040 else 1
            for index in range(len(BUTTONS)):
                for offset,widget in enumerate(table.winfo_children()[index*3:index*3+3]):
                    row=index%9 if columns==2 else index
                    col=(index//9)*3 if columns==2 else 0
                    widget.grid_configure(row=row,column=col+offset)
        table.bind('<Configure>',arrange_bindings)
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
        self.vars['prompt_style'].set('0')
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
        p=self.scroll_content(page)
        self.heading(p,'Rendering')
        self.check(p,'Compile new shaders in the background','async_shaders')
        note=self.label(p,'May cause missing objects or instability.',True)
        note.configure(wraplength=420); note.pack(fill='x',pady=(0,12))
        self.check(p,'GPU readbacks without waiting','stale_readback')
        self.check(p,'Defer GPU readbacks','deferred_readback')
        self.choice(p,'Frames queued (0 = automatic)','frames_queued',('1','2','0'))
        note=self.label(p,'Changes apply on the next launch. Background compilation defaults off for stability.',True)
        note.configure(wraplength=420); note.pack(fill='x',pady=22)
        self.button(p,'Open latest session logs',lambda:self.open_folder(self.session or ROOT/'out')).pack(anchor='w')

    def build_cheats(self):
        p=self.scroll_content(self.pages['Cheats'])
        self.heading(p,'Cheats')
        self.check(p,'Unlimited player health','unlimited_health')
        self.label(p,'Protects the player from normal damage, including bullets and explosions.',True).pack(fill='x',pady=(0,12))
        self.check(p,'Infinite ammo / no reload','infinite_ammo')
        self.label(p,'Keep ammunition available and fire without reloading.',True).pack(fill='x',pady=(0,20))
        self.check(p,'Unlimited sprint','unlimited_sprint')
        self.label(p,'Prevents stamina exhaustion while sprinting. Start sprinting with your normal control.',True).pack(fill='x',pady=(0,12))
        self.check(p,'Continuous Tactical Echo / orange enemy marks','continuous_scan')
        self.label(p,'Activate Tactical Echo once after loading. Scans then repeat at the normal range and interval to refresh enemy marks.',True).pack(fill='x',pady=(0,12))
        self.label(p,'Changes apply on the next launch. Turn a cheat off and relaunch to restore normal play. Scripted deaths may still occur.',True).pack(fill='x',pady=12)
        self.button(p,'Disable all cheats',self.reset_cheats).pack(anchor='w',pady=12)

    def reset_cheats(self):
        self.vars['unlimited_health'].set(False)
        self.vars['infinite_ammo'].set(False)
        self.vars['unlimited_sprint'].set(False)
        self.vars['continuous_scan'].set(False)

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
        if folder: self.vars['game_dir'].set(folder); self.refresh_ready()
    def browse_storage(self,key):
        initial=storage_directory(self.gather(),key)
        while not initial.is_dir() and initial!=initial.parent: initial=initial.parent
        folder=filedialog.askdirectory(initialdir=str(initial),parent=self.root,
                                       title='Choose shader cache folder' if key=='cache_dir' else 'Choose saved-data folder')
        if folder: self.vars[key].set(folder)
    def refresh_ready(self):
        ok,text=validate_game(self.vars['game_dir'].get()); self.ready.configure(text=text,fg=GREEN if ok else CYAN)
        self.game_ok=ok
        if self.monitor is None and self.preparing is None:
            self.play_button.configure(text='PLAY GAME' if ok else 'SET UP GAME')
            self.session_label.configure(text='Ready to play.' if ok else 'Select your game folder to get started.',fg=GREEN if ok else MUTED)
    def open_folder(self,path):
        path.mkdir(parents=True,exist_ok=True); os.startfile(str(path))
    def play(self):
        if self.preparing is not None: return
        if self.monitor and self.monitor.poll() is None: return
        if os.name=='nt':
            running=subprocess.run(['tasklist','/FI','IMAGENAME eq gow3-probe*','/FO','CSV','/NH'],
                capture_output=True,text=True,errors='replace',creationflags=subprocess.CREATE_NO_WINDOW)
            if 'gow3-probe' in running.stdout.lower():
                messagebox.showinfo('Game already running','Close the current Killzone game before starting another session.',parent=self.root); return
        self.refresh_ready()
        if not self.game_ok: self.show_tab('Game'); self.browse(); return
        values=self.gather()
        exe=ROOT/executable_for(values)
        for path in (exe,ROOT/'out/renderer-stability-toggles.txt'):
            if not path.is_file(): messagebox.showerror('Installation incomplete',f'A required application file is missing:\n{path.name}\n\nExtract the entire release ZIP again, then open the launcher from the extracted folder.',parent=self.root); return
        try:
            prepare_storage(values)
            persist(values)
            if needs_preparation(values['game_dir']):
                helper=ROOT/'packaging/windows/prepare_killzone.py'
                if not helper.is_file():
                    raise OSError('Game preparation helper is missing. Extract the entire release ZIP again.')
                self.preparation_log=ROOT/'out/prepare-game.log'
                # pythonw drops standard streams in preparation's child scripts;
                # use python.exe with CREATE_NO_WINDOW to retain the setup log.
                python=Path(sys.executable)
                if python.name.lower()=='pythonw.exe': python=python.with_name('python.exe')
                with self.preparation_log.open('wb') as log:
                    self.preparing=subprocess.Popen([str(python),str(helper),values['game_dir']],
                        cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,
                        creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.play_button.configure(text='PREPARING GAME',state='disabled')
                self.session_label.configure(text='Preparing your game for first launch. This may take a few minutes.',fg=CYAN)
                return
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
                            'KZSF_UNLIMITED_HEALTH','KZSF_INFINITE_AMMO','KZSF_UNLIMITED_SPRINT','KZSF_CONTINUOUS_SCAN',
                            'GOW3_OUTPUT_RES','GOW3_FRAME_GEN','GOW3_ANISOTROPY','GOW3_MIP_LOD_BIAS'):
                    log.write(f'{key}={spec["env"][key]}\n')
            self.monitor=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--run-session',str(folder/'session.json')],
                env=spec['env'],cwd=ROOT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.launched_values=values
            self.play_button.configure(text='GAME RUNNING',state='disabled'); self.session_label.configure(text='Game running. Enjoy your session.',fg=CYAN)
        except (OSError,ValueError) as exc: messagebox.showerror('Launch failed',str(exc),parent=self.root)
    def poll(self):
        if self.preparing is not None and self.preparing.poll() is not None:
            code=self.preparing.returncode
            self.preparing=None
            self.play_button.configure(text='PLAY GAME',state='normal')
            if code==0:
                self.session_label.configure(text='Game preparation complete. Starting game...',fg=GREEN)
                self.play()
            else:
                self.session_label.configure(text='Game preparation failed. Check your dump and setup log.',fg=CYAN)
                try:
                    detail=self.preparation_log.read_text(encoding='utf-8',errors='replace')[-1800:]
                except OSError:
                    detail='The setup log could not be read.'
                messagebox.showerror('Game preparation failed',f'{detail}\n\nFull log: {self.preparation_log}',parent=self.root)
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
