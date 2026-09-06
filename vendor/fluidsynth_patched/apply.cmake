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
edit_file("include/fluidsynth/synth.h" "b63d328166b9cd0ae5e8249aab880e43675d7a12a2d409c3dc43c553fc385765" "835010ecdc847d1e9475da5d7acfa7b845eceac0f07430100e19e9a3a6e564fd"
[==[FLUIDSYNTH_API float fluid_synth_get_gen(fluid_synth_t *synth, int chan, int param);]==]
[==[FLUIDSYNTH_API float fluid_synth_get_gen(fluid_synth_t *synth, int chan, int param);
/* Juicy16 extension: CC1-driven pitch LFO depth only, without rewriting MIDI. */
#define FLUIDSYNTH_JUICY16_VIBRATO_SCALE 1
FLUIDSYNTH_API int fluid_synth_set_cc1_vibrato_scale(fluid_synth_t *synth, int chan, float scale);]==])
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
