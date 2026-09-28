# Exact, hash-bracketed edits for FluidSynth 2.5.7. Shared by macOS and Windows.
if(NOT DEFINED FLUID_SOURCE)
  message(FATAL_ERROR "Pass -DFLUID_SOURCE=<pristine FluidSynth 2.5.7 source>")
endif()
function(edit_file relative base result before after)
  set(path "${FLUID_SOURCE}/${relative}")
  file(SHA256 "${path}" actual)
  if(actual STREQUAL result)
    return() # idempotent rebuild
  endif()
  if(NOT actual STREQUAL base)
    message(FATAL_ERROR "Unreviewed FluidSynth patch base: ${relative}")
  endif()
  file(READ "${path}" content)
  string(REPLACE "${before}" "${after}" content "${content}")
  string(SHA256 actual "${content}")
  if(NOT actual STREQUAL result)
    message(FATAL_ERROR "Unexpected FluidSynth patch result: ${relative}")
  endif()
  file(WRITE "${path}" "${content}")
endfunction()
edit_file("include/fluidsynth/synth.h" "b63d328166b9cd0ae5e8249aab880e43675d7a12a2d409c3dc43c553fc385765" "6aba9116e8a111e29610a0820811989267a919fc04f960620be08b176e8f0941"
[==[FLUIDSYNTH_API float fluid_synth_get_gen(fluid_synth_t *synth, int chan, int param);]==]
[==[FLUIDSYNTH_API float fluid_synth_get_gen(fluid_synth_t *synth, int chan, int param);
/* Juicy16 extension: CC1-driven pitch LFO depth only, without rewriting MIDI. */
#define FLUIDSYNTH_JUICY16_VIBRATO_SCALE 1
FLUIDSYNTH_API int fluid_synth_set_cc1_vibrato_scale(fluid_synth_t *synth, int chan, float scale);
/* Juicy16 extension: CC10 reaches both edges on every DLS region. */
#define FLUIDSYNTH_JUICY16_DLS_FULL_PAN 1]==])
edit_file("src/synth/fluid_chan.h" "cf4d1000361fd12f516e7f540a36c444d5c3101b213784cb42df66724544603a" "6a2bbb7e59e5cc3a4b3d98a537106a29d9dd97f0faa117e21f70a6882a4be4f9"
[==[    int channum;                          /**< MIDI channel number */]==]
[==[    int channum;                          /**< MIDI channel number */
    float cc1_vibrato_scale;              /* Juicy16 setting, survives MIDI resets */]==])
edit_file("src/synth/fluid_chan.c" "93a68ae0d611a35f8c6a39611cdb600ef81917068fe200261418b3a2cc3fedfb" "486a7440d9a849d4f41858cadadaaae6f56bc7c903d08b0b00995015c3b16f9d"
[==[    chan->channum = num;]==]
[==[    chan->channum = num;
    chan->cc1_vibrato_scale = 1.0f;]==])
edit_file("src/synth/fluid_mod.c" "4779920afad5fee4c86d37a1375debcc424d1f7883321c7333754931b95bb4f9" "f63be4417a919821d75a2c4d37773610d2973fb0eb7432d3c1c548f4288ea797"
[==[    return final_value;
}]==]
[==[    /* Scale only CC1 contributions to pitch LFO depth. Bank curves, secondary
     * sources, explicit zero overrides and all other destinations stay intact. */
    if((mod->dest == GEN_VIBLFOTOPITCH || mod->dest == GEN_MODLFOTOPITCH)
            && (((mod->flags1 & FLUID_MOD_CC) && mod->src1 == 1)
                || ((mod->flags2 & FLUID_MOD_CC) && mod->src2 == 1)))
    {
        final_value *= voice->channel->cc1_vibrato_scale;
    }
    return final_value;
}]==])
edit_file("src/synth/fluid_synth.c" "d4a223fba22d8b25c547652c82eab3124fb05311fac57db4dd841ba6ecd22d75" "995457c30a179ac4971b03cfe61f4f0e4666597fd6c3b45896000613e0b9908c"
[==[static void fluid_synth_reset_basic_channel_LOCAL(fluid_synth_t *synth, int chan, int nbr_chan);]==]
[==[/* Juicy16 extension. Uses the usual API lock; updates held and future voices.
 * Range validation rejects NaN as well as values outside the public control. */
int fluid_synth_set_cc1_vibrato_scale(fluid_synth_t *synth, int chan, float scale)
{
    fluid_return_val_if_fail(scale >= 1.0f && scale <= 24.0f, FLUID_FAILED);
    FLUID_API_ENTRY_CHAN(FLUID_FAILED);
    synth->channel[chan]->cc1_vibrato_scale = scale;
    fluid_synth_modulate_voices_LOCAL(synth, chan, 1, 1);
    FLUID_API_RETURN(FLUID_OK);
}

static void fluid_synth_reset_basic_channel_LOCAL(fluid_synth_t *synth, int chan, int nbr_chan);]==])
edit_file("src/sfloader/fluid_dls.cpp" "9bfb042b0170723d09403a7719fc0cd733c726a84073e97c6a1e9bd148beb73a" "f3e1291f5bcedd290fa8e849d679fc66dee2823d7a8d71dc70e94fa2cba518dd"
[==[            // See also https://github.com/FluidSynth/fluidsynth/pull/1626 conversation for "Key Number to Pitch" articulation implementation]==]
[==[            /* Juicy16: CC10 can move every region to either edge. Depth is the
             * standard 500 plus the region's own static pan, so hard-panned stereo
             * pairs follow pan fully instead of the bank's own narrower range. */
            {
                fluid_mod_t full_pan;
                fluid_mod_clone(&full_pan, &default_pan_mod);
                fluid_mod_set_amount(&full_pan, 500.0 + std::abs(art.gens[GEN_PAN].value_or(0)));
                fluid_voice_add_mod_local(voice, &full_pan, FLUID_VOICE_OVERWRITE, voice->mod_count);
            }

            // See also https://github.com/FluidSynth/fluidsynth/pull/1626 conversation for "Key Number to Pitch" articulation implementation]==])
edit_file("src/utils/fluid_sys.c" "6ac2120ae685b97cfa13ad42a2f8113ff9b88a2300f3b1237b0be1c575f2e2aa" "06f92bde0e24c4325083633f4eef710def75bc64fd7cb615e21a174b657c5b50"
[==[    if(timer->thread)
    {
        auto_destroy = timer->auto_destroy;
        fluid_thread_join(timer->thread);

        if(!auto_destroy)
        {
            timer->thread = NULL;
        }
    }]==]
[==[    /* Joining finishes the work; the C++11 thread object still needs release.
     * Capture it because an auto-destroying timer may free itself on exit. */
    fluid_thread_t *thread = timer->thread;
    if(thread)
    {
        auto_destroy = timer->auto_destroy;
        fluid_thread_join(thread);
        delete_fluid_thread(thread);

        if(!auto_destroy)
        {
            timer->thread = NULL;
        }
    }]==])
