"""Launcher behavior without starting the game or changing user preferences."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('kzsf_local',Path(__file__).resolve().parents[1]/'launcher/kzsf_local.py')
ui=importlib.util.module_from_spec(spec); spec.loader.exec_module(ui)

class LauncherTests(unittest.TestCase):
    def test_player_features_default_off_and_environment_isolated(self):
        for key in ('third_person','free_camera','model_switching'):
            self.assertFalse(ui.DEFAULTS[key])
        with patch.dict(os.environ,{key:'1' for key in ui.PLAYER_HOOK_ENV}):
            env=ui.launch_spec(dict(ui.DEFAULTS),'logs')['env']
            self.assertFalse(set(ui.PLAYER_HOOK_ENV) & set(env))

    def test_player_feature_dependencies_and_current_executable(self):
        for third,free,models in ((True,False,False),(False,True,False),(False,False,True),(True,True,True)):
            values=dict(ui.DEFAULTS,third_person=third,free_camera=free,model_switching=models)
            spec=ui.launch_spec(values,'logs');env=spec['env']
            self.assertEqual(ui.executable_for(values),ui.GRAPHICS_EXE)
            for key in ('KZSF_CAMERA_OBSERVER','KZSF_CAMERA_CALL_BRIDGE','KZSF_THIRD_PERSON_TEST'):
                self.assertEqual(env[key],'1')
            self.assertEqual(env.get('KZSF_FOLLOW_FREE_CAMERA')=='1',free)
            self.assertEqual(env.get('KZSF_CHARACTER_BUNDLE_TEST')=='1',models)
            if models:
                for key in ('KZSF_CHARACTER_OVERLAY_TEST','KZSF_CHARACTER_BODY_ROUTE_TEST',
                            'KZSF_CHARACTER_ENGINEER_DRAW_TEST','KZSF_CHARACTER_SINGLE_BODY_TEST'):
                    self.assertEqual(env[key],'1')
                self.assertEqual(env['KZSF_CHARACTER_FACE_PLAYER_ANIMATION_CONTROL'],'0')
            self.assertNotIn('KZSF_FREE_CAMERA_TEST',env) # standalone freecam conflicts with shared observer

    def test_player_feature_preferences_round_trip(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch.object(ui,'PREFS',Path(folder)/'prefs.json'),patch.object(ui,'CONFIG',Path(folder)/'settings.ini'):
                ui.persist(dict(ui.DEFAULTS,third_person=True,free_camera=True,model_switching=True))
                loaded=ui.load_preferences()
                for key in ('third_person','free_camera','model_switching'):
                    self.assertIs(loaded[key],True)

    def test_graphics_adaptation_round_trip_and_launch(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch.object(ui,'PREFS',Path(folder)/'prefs.json'), patch.object(ui,'CONFIG',Path(folder)/'settings.ini'):
                values=dict(ui.DEFAULTS,render_output='3200x1800',frame_gen='fsr3',anisotropy='16',
                            mip_lod_bias='-0.5',fsr1=True,rcas_strength='50')
                ui.persist(values)
                loaded=ui.load_preferences()
                for key in ('render_output','frame_gen','anisotropy','mip_lod_bias','fsr1','rcas_strength'):
                    self.assertEqual(loaded[key],values[key])
                env=ui.launch_spec(loaded,folder)['env']
                self.assertEqual(ui.executable_for(loaded),ui.GRAPHICS_EXE)
                self.assertEqual(ui.executable_for(dict(ui.DEFAULTS)),'out/gow3-probe-launcher.exe')
                self.assertEqual(env['GOW3_OUTPUT_RES'],'3200x1800')
                self.assertEqual(env['GOW3_UPSCALER'],'fsr3')
                self.assertEqual(env['GOW3_SCENE_NATIVE'],'1')
                self.assertEqual(env['GOW3_ANISOTROPY'],'16')
                self.assertEqual(env['GOW3_MIP_LOD_BIAS'],'-0.5')
                self.assertEqual(env['GOW3_FRAME_GEN'],'fsr3')
                self.assertNotIn('GOW3_RENDER_RES',env)
                ui.write_keys(ui.CONFIG,{'output_res':'999999x999999','anisotropy':'900',
                                        'mip_lod_bias':'nan','frame_gen':'unknown','rcas_strength':'-1'})
                loaded=ui.load_preferences()
                for key in ('render_output','anisotropy','mip_lod_bias','frame_gen','rcas_strength'):
                    self.assertEqual(loaded[key],ui.DEFAULTS[key])

    def test_frame_generation_native_and_environment_isolation(self):
        contaminated=dict(GOW3_FRAME_GEN='fsr3',GOW3_SCENE_NATIVE='1',GOW3_NATIVE_AA='1',
                          GOW3_RENDER_RES='1280x720',GOW3_OUTPUT_RES='3840x2160')
        with patch.dict(os.environ,contaminated):
            env=ui.launch_spec(dict(ui.DEFAULTS),'logs')['env']
            self.assertEqual(env['GOW3_FRAME_GEN'],'off')
            self.assertEqual(env['GOW3_OUTPUT_RES'],'1920x1080')
            self.assertNotIn('GOW3_NATIVE_AA',env)
            self.assertNotIn('GOW3_SCENE_NATIVE',env)
            env=ui.launch_spec(dict(ui.DEFAULTS,frame_gen='fsr3'),'logs')['env']
            self.assertEqual(env['GOW3_NATIVE_AA'],'1')
            self.assertEqual(env['GOW3_UPSCALER'],'fsr3')

    def test_native_prompt_settings_round_trip(self):
        with tempfile.TemporaryDirectory() as folder:
            prefs=Path(folder)/'prefs.json';config=Path(folder)/'settings.ini'
            with patch.object(ui,'PREFS',prefs),patch.object(ui,'CONFIG',config):
                for style in ('0','1','2'):
                    ui.persist(dict(ui.DEFAULTS,prompt_style=style))
                    values=ui.load_preferences()
                    self.assertEqual(values['prompt_style'],style)
                    env=ui.launch_spec(values,Path(folder)/'logs')['env']
                    self.assertEqual(env['KZSF_PROMPT_STYLE'],style)
                    self.assertEqual(env['KZSF_NATIVE_PROMPTS'],'1')
                ui.write_keys(config,{'prompt_style':'garbage'})
                self.assertEqual(ui.load_preferences()['prompt_style'],'0')
    def test_launch_profiles_and_safety_settings(self):
        for profile,exe in ui.PROFILES.items():
            values=dict(ui.DEFAULTS,profile=profile,fps='120',keys={'cross':'Space'},pads={})
            spec=ui.launch_spec(values,Path('logs'))
            self.assertTrue(spec['command'][0].endswith(exe.replace('/',os.sep)))
            env=spec['env']
            self.assertEqual(env['GOW3_FPS_LIMIT'],'120')
            self.assertEqual(env['KZSF_VIRTUAL_TSC'],'1')
            self.assertEqual(env['GOW3_DRAW_PIPE'],'0')
            self.assertEqual(env['GOW3_ASYNC_SHADERS'],'0')
            self.assertEqual(env['GOW3_UPSCALER'],'none')
            self.assertEqual(env['GOW3_READBACK_LINEAR'],'1')
            self.assertEqual(env['GOW3_DMEM_MB'],'4608' if profile.startswith('Beta') else '6144')
            self.assertEqual(env['GOW3_USER_DIR'],str(ui.ROOT/'user'))

    def test_preferences_and_ini_preserve_unexposed_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            prefs=Path(folder)/'prefs.json'; config=Path(folder)/'settings.ini'
            config.write_text('engine_fps=120\nunknown_key=keep\nvsync=1\n')
            with patch.object(ui,'PREFS',prefs),patch.object(ui,'CONFIG',config):
                ui.persist(dict(ui.DEFAULTS,fps='120',vsync=False))
                self.assertEqual(ui.load_preferences()['fps'],'120')
                self.assertIn('unknown_key=keep',config.read_text())
                self.assertIn('engine_fps=120',config.read_text())
                self.assertIn('vsync=0',config.read_text())
                prefs.write_text('{broken')
                self.assertEqual(ui.load_preferences()['profile'],'Current build')

    def test_assists_are_independent_and_use_verified_build(self):
        for health,ammo in ((True,False),(False,True),(True,True)):
            values=dict(ui.DEFAULTS,unlimited_health=health,infinite_ammo=ammo)
            spec=ui.launch_spec(values,Path('logs'))
            self.assertEqual(Path(spec['command'][0]).name,'gow3-probe-launcher.exe')
            self.assertEqual(spec['env']['KZSF_UNLIMITED_HEALTH'],str(int(health)))
            self.assertEqual(spec['env']['KZSF_INFINITE_AMMO'],str(int(ammo)))

    def test_engine_look_rate_controls_plumb_heading_and_pitch(self):
        for key in ('controller_turn_speed','controller_pitch_speed'):
            values=dict(ui.DEFAULTS,**{key:'150'})
            spec=ui.launch_spec(values,Path('logs'))
            self.assertEqual(Path(spec['command'][0]).name,'gow3-probe-launcher.exe')
            self.assertEqual(spec['env']['KZSF_PLAYER_TURN'],'1')
            self.assertNotIn('KZSF_ENGINE_TURN',spec['env'])
            self.assertEqual(spec['env']['GOW3_PAD_TURN_SPEED'],
                             '150' if key=='controller_turn_speed' else '100')
            self.assertEqual(spec['env']['GOW3_PAD_PITCH_SPEED'],
                             '150' if key=='controller_pitch_speed' else '100')

    def test_current_build_and_fsr_flags_are_isolated(self):
        inherited=dict(KZSF_FSR_QUALITY='1',KZSF_FSR_NATIVE='1',GOW3_RENDER_RES='1280x720',
                       GOW3_PRESET_FILE='old-test',KZSF_ENGINE_TURN='1',KZSF_PLAYER_TURN='1',
                       KZSF_CAMERA_OBSERVER='1',KZSF_CAMERA_CALL_BRIDGE='1',KZSF_THIRD_PERSON_TEST='1',KZSF_THIRD_PERSON_AUTOTEST='1')
        with patch.dict(os.environ,inherited):
            for provider in ('none','fsr3','taa'):
                launch=ui.launch_spec(dict(ui.DEFAULTS,upscaler=provider),Path('logs'))
                env=launch['env']
                self.assertEqual(Path(launch['command'][0]).name,'gow3-probe-launcher.exe')
                for key in ('KZSF_FSR_QUALITY','GOW3_RENDER_RES','GOW3_PRESET_FILE','KZSF_ENGINE_TURN',
                            'KZSF_CAMERA_OBSERVER','KZSF_CAMERA_CALL_BRIDGE','KZSF_THIRD_PERSON_TEST','KZSF_THIRD_PERSON_AUTOTEST'):
                    self.assertNotIn(key,env)
                self.assertEqual(env['KZSF_PLAYER_TURN'],'1')
                self.assertEqual(env['GOW3_PRECOMPILE'],'1')
                self.assertEqual(env['KZSF_DIRECT_TSC'],'1')
                self.assertEqual(env['GOW3_FREEZE_SECONDS'],'0')
                if provider in ('fsr3','taa'):
                    self.assertEqual(env['KZSF_FSR_NATIVE'],'1')
                    self.assertEqual(env['GOW3_UPSCALE_PRESET'],'0')
                    self.assertEqual(env['GOW3_JITTER'],'0')
                else:
                    self.assertNotIn('KZSF_FSR_NATIVE',env)

    def test_post_effect_switches_default_on_and_map_to_disable_flags(self):
        spec=ui.launch_spec(dict(ui.DEFAULTS),Path('logs'))
        # Defaults must be the original behaviour: effects ON -> disable flags 0.
        self.assertEqual(spec['env']['KZSF_DISABLE_BLOOM'],'0')
        self.assertEqual(spec['env']['KZSF_DISABLE_FILM_GRAIN'],'0')
        spec=ui.launch_spec(dict(ui.DEFAULTS,bloom=False,film_grain=False),Path('logs'))
        self.assertEqual(spec['env']['KZSF_DISABLE_BLOOM'],'1')
        self.assertEqual(spec['env']['KZSF_DISABLE_FILM_GRAIN'],'1')
        with tempfile.TemporaryDirectory() as folder:
            prefs=Path(folder)/'prefs.json'; config=Path(folder)/'settings.ini'
            config.write_text('engine_fps=120\n')
            with patch.object(ui,'PREFS',prefs),patch.object(ui,'CONFIG',config):
                ui.persist(dict(ui.DEFAULTS,bloom=False,film_grain=False))
                saved=config.read_text()
                self.assertIn('bloom=0',saved); self.assertIn('film_grain=0',saved)
                loaded=ui.load_preferences()
                self.assertIs(loaded['bloom'],False); self.assertIs(loaded['film_grain'],False)

    def test_campaign_cheats_are_opt_in_independent_and_persist(self):
        for sprint,scan in ((False,False),(True,False),(False,True),(True,True)):
            values=dict(ui.DEFAULTS,unlimited_sprint=sprint,continuous_scan=scan)
            with patch.dict(os.environ,KZSF_UNLIMITED_SPRINT='1',KZSF_CONTINUOUS_SCAN='1'):
                spec=ui.launch_spec(values,Path('logs'))
            self.assertEqual(spec['env']['KZSF_UNLIMITED_SPRINT'],str(int(sprint)))
            self.assertEqual(spec['env']['KZSF_CONTINUOUS_SCAN'],str(int(scan)))
            expected=ui.GRAPHICS_EXE if sprint or scan else ui.PROFILES[values['profile']]
            self.assertEqual(Path(spec['command'][0]).name,Path(expected).name)
        with tempfile.TemporaryDirectory() as folder:
            prefs=Path(folder)/'prefs.json'; config=Path(folder)/'settings.ini'
            config.write_text('unknown_key=keep\n')
            with patch.object(ui,'PREFS',prefs),patch.object(ui,'CONFIG',config):
                ui.persist(dict(ui.DEFAULTS,unlimited_sprint=True,continuous_scan=True))
                loaded=ui.load_preferences()
                self.assertIs(loaded['unlimited_sprint'],True)
                self.assertIs(loaded['continuous_scan'],True)
                self.assertIn('unknown_key=keep',config.read_text())

    def test_invalid_game_metadata_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder)/'eboot.bin').touch()
            with patch.object(ui,'read_sfo',return_value=None): self.assertFalse(ui.validate_game(folder)[0])
            # A supported title/version still has to pass the catalog build check.
            with patch.object(ui,'read_sfo',return_value={'TITLE_ID':'CUSA00190','APP_VER':'01.81'}), \
                 patch.object(ui,'verify_game_build',side_effect=ValueError('unsupported build')):
                self.assertFalse(ui.validate_game(folder)[0])
            with patch.object(ui,'read_sfo',return_value={'TITLE_ID':'CUSA00190','APP_VER':'01.81'}), \
                 patch.object(ui,'verify_game_build'):
                self.assertTrue(ui.validate_game(folder)[0])

    def test_independent_monitor_records_failure_and_output(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder)
            manifest=folder/'session.json'
            manifest.write_text(json.dumps(dict(command=[sys.executable,'-c',"print('captured output');raise SystemExit(23)"],cwd=str(folder),folder=str(folder))))
            ui.run_session(manifest)
            self.assertEqual(json.loads((folder/'status.json').read_text())['exit_code'],23)
            self.assertIn('captured output',(folder/'stdout.log').read_text())
            self.assertIn('Exit code: 23',(folder/'run.txt').read_text())

if __name__=='__main__': unittest.main()
