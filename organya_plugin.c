///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Organya Playback Plugin
//
// Implements RVPlaybackPlugin interface for Organya music files (Cave Story engine).
// Supported formats: org (Org-01, Org-02, Org-03)
// Audio output: Stereo F32 at 48000 Hz (native output from organya.h).
//
// Note: Requires a wavetable soundbank (.wdb) for proper playback. Without it, the plugin
// generates basic waveforms for melody channels and percussion will be silent.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef nullptr
#define nullptr ((void*)0)
#endif

#define ORG_USE_STDINT
#define ORG_NO_STDIO
#include "organya.h"

#include <retrovert/io.h>
#include <retrovert/log.h>
#include <retrovert/metadata.h>
#include <retrovert/playback.h>
#include <retrovert/service.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ORG_SAMPLE_RATE 48000
#define ORG_CHANNELS 2
#define ORG_BUFFER_SIZE 4096
#define ORG_DEFAULT_DURATION_S 180 // 3 minutes default for looping songs

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

RV_PLUGIN_USE_IO_API();
RV_PLUGIN_USE_LOG_API();
RV_PLUGIN_USE_METADATA_API();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    organya_context ctx;
    int initialized;
    int song_loaded;
    int elapsed_frames;
    int max_frames; // Based on default duration
    bool scope_enabled;
} OrganyaReplayerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* organya_plugin_supported_extensions(void) {
    return "org";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void organya_plugin_static_init(const RVService* service_api) {
    rv_init_log_api(service_api);
    rv_init_io_api(service_api);
    rv_init_metadata_api(service_api);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generate basic waveforms for melody channels when no .wdb soundbank is available.
// This provides functional playback with standard waveforms (sine, triangle, square, etc.)
// Percussion channels will be silent without a proper .wdb file.

static void organya_generate_default_wavetable(organya_context* ctx) {
    // Generate 100 waveforms, each 256 bytes of signed 8-bit data (-128 to 127)
    // Use a mix of standard waveforms with varying harmonic content
    for (int wave = 0; wave < ORG_WAVETABLE_COUNT; wave++) {
        uint8_t* data = &ctx->melody_wave_data[wave * 0x100];

        if (wave < 6) {
            // Pure sine wave
            for (int i = 0; i < 256; i++) {
                double phase = (double)i / 256.0 * 2.0 * M_PI;
                data[i] = (uint8_t)(int8_t)(sin(phase) * 64.0);
            }
        } else if (wave < 12) {
            // Triangle wave
            for (int i = 0; i < 256; i++) {
                if (i < 64) {
                    data[i] = (uint8_t)(int8_t)(i * 2);
                } else if (i < 192) {
                    data[i] = (uint8_t)(int8_t)(127 - (i - 64) * 2);
                } else {
                    data[i] = (uint8_t)(int8_t)(-128 + (i - 192) * 2);
                }
            }
        } else if (wave < 18) {
            // Square wave with varying duty cycle
            int duty = 128 + (wave - 12) * 10;
            if (duty > 230) {
                duty = 230;
            }
            for (int i = 0; i < 256; i++) {
                data[i] = (uint8_t)(int8_t)(i < duty ? 64 : -64);
            }
        } else if (wave < 24) {
            // Sawtooth
            for (int i = 0; i < 256; i++) {
                data[i] = (uint8_t)(int8_t)(i / 2 - 64);
            }
        } else {
            // Harmonics blend (wave index determines harmonic mix)
            int harmonics = 1 + (wave % 8);
            for (int i = 0; i < 256; i++) {
                double val = 0.0;
                for (int h = 1; h <= harmonics; h++) {
                    double phase = (double)i / 256.0 * 2.0 * M_PI * h;
                    val += sin(phase) / h;
                }
                val = val * 50.0 / harmonics;
                if (val > 127.0) {
                    val = 127.0;
                }
                if (val < -128.0) {
                    val = -128.0;
                }
                data[i] = (uint8_t)(int8_t)val;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* organya_plugin_create(const RVService* service_api) {
    OrganyaReplayerData* data = malloc(sizeof(OrganyaReplayerData));
    if (data == nullptr) {
        return nullptr;
    }
    memset(data, 0, sizeof(OrganyaReplayerData));

    organya_result res = organya_context_init(&data->ctx);
    if (res != ORG_RESULT_SUCCESS) {
        rv_error("Organya: context init failed (%d)", (int)res);
        free(data);
        return nullptr;
    }

    organya_context_set_sample_rate(&data->ctx, ORG_SAMPLE_RATE);
    organya_context_set_interpolation(&data->ctx, ORG_INTERPOLATION_LAGRANGE);

    // Generate default wavetable (basic waveforms)
    organya_generate_default_wavetable(&data->ctx);

    data->initialized = 1;
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int organya_plugin_destroy(void* user_data) {
    OrganyaReplayerData* data = (OrganyaReplayerData*)user_data;
    if (data->initialized) {
        organya_context_deinit(&data->ctx);
    }
    free(data);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVProbeResult organya_plugin_probe_can_play(uint8_t* probe_data, uint64_t data_size, const char* url,
                                                   uint64_t total_size) {
    (void)url;
    (void)total_size;

    // Check for "Org-" magic followed by version "01", "02", or "03"
    if (data_size >= 6) {
        if (probe_data[0] == 'O' && probe_data[1] == 'r' && probe_data[2] == 'g' && probe_data[3] == '-'
            && probe_data[4] == '0' && (probe_data[5] == '1' || probe_data[5] == '2' || probe_data[5] == '3')) {
            return RVProbeResult_Supported;
        }
    }

    return RVProbeResult_Unsupported;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int organya_plugin_open(void* user_data, const char* url, uint32_t subsong, const RVService* service_api) {
    (void)subsong;
    (void)service_api;

    OrganyaReplayerData* data = (OrganyaReplayerData*)user_data;

    // Unload previous song
    if (data->song_loaded) {
        organya_context_unload_song(&data->ctx);
        data->song_loaded = 0;
    }

    RVIoReadUrlResult read_res;
    if ((read_res = rv_io_read_url_to_memory(url)).data == nullptr) {
        rv_error("Organya: Failed to load %s", url);
        return -1;
    }

    organya_result res = organya_context_read_song(&data->ctx, (const uint8_t*)read_res.data, read_res.data_size);
    rv_io_free_url_to_memory(read_res.data);

    if (res != ORG_RESULT_SUCCESS) {
        rv_error("Organya: Failed to read song %s (%d)", url, (int)res);
        return -1;
    }

    data->song_loaded = 1;
    data->elapsed_frames = 0;
    data->max_frames = ORG_DEFAULT_DURATION_S * ORG_SAMPLE_RATE;

    // Seek to beginning
    organya_context_seek(&data->ctx, 0);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void organya_plugin_close(void* user_data) {
    OrganyaReplayerData* data = (OrganyaReplayerData*)user_data;
    // Note: Don't call organya_context_unload_song() here because
    // destroy() → organya_context_deinit() will call it. The organya
    // library doesn't null pointers after freeing, so calling unload_song
    // in both close() and deinit() causes double-free.
    data->song_loaded = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVReadInfo organya_plugin_read_data(void* user_data, RVReadData dest) {
    OrganyaReplayerData* data = (OrganyaReplayerData*)user_data;

    RVAudioFormat format = { RVAudioStreamFormat_F32, ORG_CHANNELS, ORG_SAMPLE_RATE };

    if (!data->song_loaded) {
        return (RVReadInfo) { format, 0, RVReadStatus_Error};
    }

    // Check if we've exceeded default duration (songs loop forever)
    if (data->elapsed_frames >= data->max_frames) {
        return (RVReadInfo) { format, 0, RVReadStatus_Finished};
    }

    uint32_t max_frames = dest.channels_output_max_bytes_size / (sizeof(float) * ORG_CHANNELS);
    if (max_frames > ORG_BUFFER_SIZE) {
        max_frames = ORG_BUFFER_SIZE;
    }

    // Limit to remaining frames
    int remaining = data->max_frames - data->elapsed_frames;
    if ((int)max_frames > remaining) {
        max_frames = (uint32_t)remaining;
    }

    float* output = (float*)dest.channels_output;

    // organya outputs interleaved stereo F32 directly
    size_t generated = organya_context_generate_samples(&data->ctx, output, (size_t)max_frames);

    data->elapsed_frames += (int)generated;

    RVReadStatus status;
    if (generated == 0 || data->elapsed_frames >= data->max_frames) {
        status = RVReadStatus_Finished;
    } else {
        status = RVReadStatus_Ok;
    }

    return (RVReadInfo) { format, (uint32_t)generated, status};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int64_t organya_plugin_seek(void* user_data, int64_t ms) {
    OrganyaReplayerData* data = (OrganyaReplayerData*)user_data;

    if (!data->song_loaded) {
        return -1;
    }

    // Convert ms to tick position
    // tick_duration_ms = song.tempo_ms; position = ms / tempo_ms
    uint16_t tempo_ms = data->ctx.song.tempo_ms;
    if (tempo_ms == 0) {
        tempo_ms = 1;
    }
    uint32_t tick_pos = (uint32_t)(ms / tempo_ms);

    organya_context_seek(&data->ctx, tick_pos);
    data->elapsed_frames = (int)(ms * ORG_SAMPLE_RATE / 1000);

    return ms;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int organya_plugin_metadata(const char* url, const RVService* service_api) {
    (void)service_api;

    RVMetadataId id = rv_metadata_create_url(url);
    rv_metadata_set_tag(id, RV_METADATA_SONGTYPE_TAG, "Organya");

    // Organya files don't contain embedded metadata (title/artist).
    // Use default duration since songs loop forever.
    rv_metadata_set_tag_f64(id, RV_METADATA_LENGTH_TAG, (double)ORG_DEFAULT_DURATION_S);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void organya_plugin_event(void* user_data, uint8_t* event_data, uint64_t len) {
    (void)user_data;
    (void)event_data;
    (void)len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t organya_plugin_get_scope_data(void* user_data, int channel, float* buffer, uint32_t num_samples) {
    OrganyaReplayerData* data = (OrganyaReplayerData*)user_data;
    if (data == nullptr || buffer == nullptr) {
        return 0;
    }

    if (!data->scope_enabled) {
        organya_enable_scope_capture(&data->ctx, 1);
        data->scope_enabled = true;
    }

    return organya_get_scope_data(&data->ctx, channel, buffer, num_samples);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t organya_plugin_get_scope_channel_names(void* user_data, const char** names, uint32_t max_channels) {
    (void)user_data;
    static const char* s_names[] = {
        "Melody 1", "Melody 2", "Melody 3", "Melody 4", "Melody 5", "Melody 6", "Melody 7", "Melody 8",
        "Perc 1",   "Perc 2",   "Perc 3",   "Perc 4",   "Perc 5",   "Perc 6",   "Perc 7",   "Perc 8",
    };
    uint32_t count = 16;
    if (count > max_channels)
        count = max_channels;
    for (uint32_t i = 0; i < count; i++)
        names[i] = s_names[i];
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RVPlaybackPlugin g_organya_plugin = {
    RV_PLAYBACK_PLUGIN_API_VERSION,
    "organya",
    "0.0.1",
    "organya.h (Strultz)",
    organya_plugin_probe_can_play,
    organya_plugin_supported_extensions,
    organya_plugin_create,
    organya_plugin_destroy,
    organya_plugin_event,
    organya_plugin_open,
    organya_plugin_close,
    organya_plugin_read_data,
    organya_plugin_seek,
    organya_plugin_metadata,
    organya_plugin_static_init,
    nullptr, // settings_updated
    nullptr, // get_tracker_info
    nullptr, // get_pattern_cell
    nullptr, // get_pattern_num_rows
    organya_plugin_get_scope_data,
    nullptr, // static_destroy
    organya_plugin_get_scope_channel_names,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RV_EXPORT RVPlaybackPlugin* rv_playback_plugin(void) {
    return &g_organya_plugin;
}
