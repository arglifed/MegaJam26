"""
MegaJam26 Issue #24: builds the two audio assets the user approved by name, and nothing else:

  * /Game/MegaJam26/Audio/SW_SFX_CinematicWhoosh  (imported from RawAssets/Audio/SFX/SFX_CinematicWhoosh.wav)
  * /Game/MegaJam26/Audio/MS_DirectorScore         (MetaSound Source built with the MetaSound Builder API)

Run headless with the editor CLOSED, from the project root (PowerShell):

    & "E:\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "$PWD\\MegaJam26.uproject" -ExecutePythonScript="$PWD\\Scripts\\build_director_score.py" `
        -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput

Search the log for "[DirectorScore]". Re-running rebuilds MS_DirectorScore from scratch (it is fully generated).

MS_DirectorScore contract (AMegaJamFlowPawn is the only caller; it owns ALL music + SFX while playing - Option A):

  Original inputs (kept): ActiveTake, FlowRatio, SpeedRatio (Float); OnHardCut, OnGraze, OnAspectSwitch, OnCrashed (Trigger)
  Music:  MusicWave (WaveAsset; C++ picks it from SceneMusicTracks), OnSceneChanged (Trigger, restarts the loop),
          SceneGenre (Int32, informational), MusicPitchSemitones (Float), MusicVolume (Float, linear gain)
  SFX:    one Trigger + one WaveAsset input per cue (C++ pushes its sound properties as the waves in BeginPlay),
          ChainPitchSemitones (Float, set by C++ right before OnBeatSplice / OnBeatCut)

  Music chain: Wave Player (looping) -> high-pass (ActiveTake smoothed ~100 ms: Director's Take thins the low end, no
               clicks on take cuts) -> low-pass (FlowRatio: muffled when stopped/crashed, fully open at speed) -> mixer
  Each SFX cue has its own one-shot Wave Player; everything is summed by two stereo mixers into the stereo output.
"""

import math
import os
import traceback

import unreal

AUDIO = "/Game/MegaJam26/Audio"
SCORE_NAME = "MS_DirectorScore"
WHOOSH_NAME = "SW_SFX_CinematicWhoosh"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
WHOOSH_WAV = os.path.join(PROJECT_DIR, "RawAssets", "Audio", "SFX", "SFX_CinematicWhoosh.wav")

eal = unreal.EditorAssetLibrary
SUCCEEDED = unreal.MetaSoundBuilderResult.SUCCEEDED
errors = []


def log(msg):
    unreal.log("[DirectorScore] " + msg)


def err(msg):
    errors.append(msg)
    unreal.log_error("[DirectorScore] ERROR: " + msg)


def semis(pitch_multiplier):
    return 12.0 * math.log2(pitch_multiplier)


# ---------------------------------------------------------------------------------------------------------------------
# 1) SW_SFX_CinematicWhoosh
# ---------------------------------------------------------------------------------------------------------------------
def import_whoosh():
    path = AUDIO + "/" + WHOOSH_NAME
    if eal.does_asset_exist(path):
        log("Reusing existing " + path)
        return eal.load_asset(path)
    if not os.path.isfile(WHOOSH_WAV):
        err("Missing source WAV " + WHOOSH_WAV)
        return None
    task = unreal.AssetImportTask()
    task.filename = WHOOSH_WAV
    task.destination_path = AUDIO
    task.destination_name = WHOOSH_NAME
    task.automated = True
    task.replace_existing = False
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    wave = eal.load_asset(path)
    if wave:
        log("Imported " + path)
    else:
        err("Import failed for " + path)
    return wave


# ---------------------------------------------------------------------------------------------------------------------
# 2) MS_DirectorScore
# ---------------------------------------------------------------------------------------------------------------------
class Graph(object):
    """Thin, readable wrapper over UMetaSoundSourceBuilder that logs every failed step instead of raising."""

    def __init__(self, builder):
        self.b = builder

    def node(self, namespace, name, variant="", major=1):
        cls = unreal.MetasoundFrontendClassName()
        cls.namespace = namespace
        cls.name = name
        cls.variant = variant
        handle, result = self.b.add_node_by_class_name(cls, major)
        if result != SUCCEEDED:
            err("add node %s.%s.%s v%d" % (namespace, name, variant, major))
        return handle

    def pin_in(self, node, pin):
        handle, result = self.b.find_node_input_by_name(node, pin)
        if result != SUCCEEDED:
            err("find input pin '%s'" % pin)
        return handle

    def pin_out(self, node, pin):
        handle, result = self.b.find_node_output_by_name(node, pin)
        if result != SUCCEEDED:
            err("find output pin '%s'" % pin)
        return handle

    def connect(self, out_handle, in_handle, what):
        if self.b.connect_nodes(out_handle, in_handle) != SUCCEEDED:
            err("connect " + what)

    def wire(self, src_node, src_pin, dst_node, dst_pin):
        self.connect(self.pin_out(src_node, src_pin), self.pin_in(dst_node, dst_pin), "%s -> %s" % (src_pin, dst_pin))

    def default(self, node, pin, literal):
        if self.b.set_node_input_default(self.pin_in(node, pin), literal) != SUCCEEDED:
            err("default for pin '%s'" % pin)

    def graph_input(self, name, data_type, literal):
        handle, result = self.b.add_graph_input_node(name, data_type, literal, False)
        if result != SUCCEEDED:
            err("graph input " + name)
        return handle


def build_score(whoosh):
    sub = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)

    def f(value):
        return sub.create_float_meta_sound_literal(value)[0]

    def b(value):
        return sub.create_bool_meta_sound_literal(value)[0]

    def i(value):
        return sub.create_int_meta_sound_literal(value)[0]

    def obj(value):
        return sub.create_object_meta_sound_literal(value)

    none_literal = unreal.MetasoundFrontendLiteral()

    def wave(name):
        return eal.load_asset(AUDIO + "/" + name)

    created = sub.create_source_builder(
        "DirectorScoreBuilder", output_format=unreal.MetaSoundOutputAudioFormat.STEREO, is_one_shot=False)
    log("create_source_builder returned: %s" % (", ".join(type(x).__name__ for x in created),))
    builder, on_play, _on_finished, audio_outs, result = created
    if result != SUCCEEDED or builder is None:
        err("create_source_builder")
        return
    g = Graph(builder)

    # ---- Graph inputs -------------------------------------------------------------------------------------------
    # The original 7 parameters C++ has always sent (kept so nothing logs "unknown parameter").
    original = {}
    for name in ("ActiveTake", "FlowRatio", "SpeedRatio"):
        original[name] = g.graph_input(name, "Float", f(0.0))
    for name in ("OnHardCut", "OnGraze", "OnAspectSwitch", "OnCrashed"):
        original[name] = g.graph_input(name, "Trigger", none_literal)

    # Music
    g.graph_input("SceneGenre", "Int32", i(0))
    in_scene_changed = g.graph_input("OnSceneChanged", "Trigger", none_literal)
    in_music_pitch = g.graph_input("MusicPitchSemitones", "Float", f(0.0))
    in_music_volume = g.graph_input("MusicVolume", "Float", f(0.9))
    in_music_wave = g.graph_input("MusicWave", "WaveAsset", obj(wave("SW_Music_TooClever")))
    in_chain_pitch = g.graph_input("ChainPitchSemitones", "Float", f(0.0))

    # ---- Music chain --------------------------------------------------------------------------------------------
    music_start = g.node("TriggerAny", "Trigger Any (2)")
    g.connect(on_play, g.pin_in(music_start, "In 0"), "OnPlay -> music start")
    g.connect(in_scene_changed, g.pin_in(music_start, "In 1"), "OnSceneChanged -> music start")

    music = g.node("UE", "Wave Player", "Stereo")
    g.wire(music_start, "Out", music, "Play")
    g.connect(in_music_wave, g.pin_in(music, "Wave Asset"), "MusicWave")
    g.connect(in_music_pitch, g.pin_in(music, "Pitch Shift"), "MusicPitchSemitones")
    g.default(music, "Loop", b(True))

    # ActiveTake -> ~100 ms smoothing -> high-pass cutoff 20 Hz (Studio Take) .. 250 Hz (Director's Take)
    take_smooth = g.node("UE", "InterpTo", "Audio")
    g.connect(original["ActiveTake"], g.pin_in(take_smooth, "Target"), "ActiveTake")
    g.default(take_smooth, "Interp Time", f(0.1))
    take_cutoff = g.node("MapRange", "MapRange", "Float")
    g.wire(take_smooth, "Value", take_cutoff, "In")
    for pin, value in (("In Range A", 0.0), ("In Range B", 1.0), ("Out Range A", 20.0), ("Out Range B", 250.0)):
        g.default(take_cutoff, pin, f(value))
    g.default(take_cutoff, "Clamped", b(True))

    # FlowRatio -> low-pass cutoff 4 kHz (stopped / crashed) .. 20 kHz (full speed)
    flow_cutoff = g.node("MapRange", "MapRange", "Float")
    g.connect(original["FlowRatio"], g.pin_in(flow_cutoff, "In"), "FlowRatio")
    for pin, value in (("In Range A", 0.0), ("In Range B", 1.0), ("Out Range A", 4000.0), ("Out Range B", 20000.0)):
        g.default(flow_cutoff, pin, f(value))
    g.default(flow_cutoff, "Clamped", b(True))

    music_out = {}
    for side, pin in (("L", "Out Left"), ("R", "Out Right")):
        hpf = g.node("UE", "One-Pole High Pass Filter", "Audio")
        lpf = g.node("UE", "One-Pole Low Pass Filter", "Audio")
        g.wire(music, pin, hpf, "In")
        g.wire(take_cutoff, "Out Value", hpf, "Cutoff Frequency")
        g.wire(hpf, "Out", lpf, "In")
        g.wire(flow_cutoff, "Out Value", lpf, "Cutoff Frequency")
        music_out[side] = lpf

    # ---- SFX cues -----------------------------------------------------------------------------------------------
    # (trigger input, wave input, default wave asset, pitch in semitones or "chain", gain) - values mirror the C++
    # fallback PlaySound2D calls so both audio paths sound the same.
    cues = [
        ("OnRunStart", "RunStartWave", "SW_SFX_ClapperboardHit", 0.0, 1.0),
        ("OnFilmFrame", "FilmFrameWave", "SW_SFX_FuturisticSell", 0.0, 1.0),
        ("OnDollyBoost", "DollyBoostWave", "SW_SFX_PositiveRise", 0.0, 1.0),
        ("OnDollyBoost", "DollyBoostWave", "SW_SFX_PositiveRise", semis(1.6), 1.0),  # surge layer (TriggerDollyBoost)
        ("OnDirectorsCutEnter", "DirectorsCutEnterWave", WHOOSH_NAME, 0.0, 1.05),
        ("OnDirectorsCutExit", "DirectorsCutExitWave", "SW_SFX_ClapperboardHit", semis(0.85), 0.5),
        ("OnBeatSplice", "BeatSpliceWave", "SW_SFX_PositiveRise", "chain", 1.0),
        ("OnBeatCut", "BeatCutWave", "SW_SFX_FuturisticSell", "chain", 0.95),
        ("OnCrashed", "CrashWave", "SW_SFX_VocalCut", 0.0, 1.0),
        ("OnCompleted", "VictoryWave", "SW_SFX_PositiveRise", semis(0.75), 1.0),
    ]

    trigger_handles = {"OnCrashed": original["OnCrashed"]}
    wave_handles = {}
    players = []
    for trigger, wave_input, default_wave, pitch, gain in cues:
        if trigger not in trigger_handles:
            trigger_handles[trigger] = g.graph_input(trigger, "Trigger", none_literal)
        if wave_input not in wave_handles:
            asset = whoosh if default_wave == WHOOSH_NAME else wave(default_wave)
            wave_handles[wave_input] = g.graph_input(wave_input, "WaveAsset", obj(asset))

        player = g.node("UE", "Wave Player", "Stereo")
        g.connect(trigger_handles[trigger], g.pin_in(player, "Play"), trigger)
        g.connect(wave_handles[wave_input], g.pin_in(player, "Wave Asset"), wave_input)
        if pitch == "chain":
            g.connect(in_chain_pitch, g.pin_in(player, "Pitch Shift"), "ChainPitchSemitones -> " + trigger)
        else:
            g.default(player, "Pitch Shift", f(pitch))
        players.append((player, gain))

    # ---- Mix: A = music + first 7 cues, B = A + remaining 3 cues ------------------------------------------------
    mix_a = g.node("AudioMixer", "Audio Mixer (Stereo, 8)")
    g.wire(music_out["L"], "Out", mix_a, "In 0 L")
    g.wire(music_out["R"], "Out", mix_a, "In 0 R")
    g.connect(in_music_volume, g.pin_in(mix_a, "Gain 0"), "MusicVolume")
    for slot, (player, gain) in enumerate(players[:7], start=1):
        g.wire(player, "Out Left", mix_a, "In %d L" % slot)
        g.wire(player, "Out Right", mix_a, "In %d R" % slot)
        g.default(mix_a, "Gain %d" % slot, f(gain))

    mix_b = g.node("AudioMixer", "Audio Mixer (Stereo, 4)")
    g.wire(mix_a, "Out L", mix_b, "In 0 L")
    g.wire(mix_a, "Out R", mix_b, "In 0 R")
    g.default(mix_b, "Gain 0", f(1.0))
    for slot, (player, gain) in enumerate(players[7:], start=1):
        g.wire(player, "Out Left", mix_b, "In %d L" % slot)
        g.wire(player, "Out Right", mix_b, "In %d R" % slot)
        g.default(mix_b, "Gain %d" % slot, f(gain))

    if len(audio_outs) != 2:
        err("expected 2 stereo output handles, got %d" % len(audio_outs))
    else:
        g.connect(g.pin_out(mix_b, "Out L"), audio_outs[0], "mix -> Out Left")
        g.connect(g.pin_out(mix_b, "Out R"), audio_outs[1], "mix -> Out Right")

    if errors:
        err("graph had %d error(s); NOT writing %s" % (len(errors), SCORE_NAME))
        return

    # ---- Write the asset (MS_DirectorScore is fully generated, so a rebuild replaces it) -------------------------
    path = AUDIO + "/" + SCORE_NAME
    if eal.does_asset_exist(path):
        log("Replacing existing " + path)
        eal.delete_asset(path)
    editor_sub = unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem)
    asset, result = editor_sub.build_to_asset(builder, "MegaJam26", SCORE_NAME, AUDIO)
    if result != SUCCEEDED or not asset:
        err("build_to_asset")
        return
    eal.save_asset(path, only_if_is_dirty=False)
    log("Built and saved " + path)


try:
    whoosh_asset = import_whoosh()
    build_score(whoosh_asset)
except Exception as exc:  # noqa: BLE001 - log everything, the run is headless
    err("%s\n%s" % (exc, traceback.format_exc()))

log("DONE with %d error(s)" % len(errors))
