// Combined audio implementation file
// This file contains both miniaudio and Web Audio API implementations
// Compiled conditionally based on the target platform

#ifndef __EMSCRIPTEN__
// Desktop/miniaudio implementation
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#endif

#ifdef __EMSCRIPTEN__
// Web Audio API implementation for Emscripten
#include <emscripten.h>

// Internal EM_JS functions with unique names
EM_JS(void, web_audio_init_internal, (), {
    if (window.gameAudio) return;
    window.gameAudio = {
        ctx: null,
        bgmSource: null,
        bgmBuffer: null,
        bgmGain: null,
        muted: false,
        volume: 0.5,
        sfxBuffers: {},
        pendingBgmPlay: false  // Track if play was requested before buffer ready
    };
});

EM_JS(void, web_audio_load_bgm_internal, (const char* path_ptr), {
    if (!window.gameAudio) return;
    
    if (!window.gameAudio.ctx) {
        window.gameAudio.ctx = new (window.AudioContext || window.webkitAudioContext)();
        window.gameAudio.bgmGain = window.gameAudio.ctx.createGain();
        window.gameAudio.bgmGain.connect(window.gameAudio.ctx.destination);
        window.gameAudio.bgmGain.gain.value = window.gameAudio.volume;
    }
    
    var path = UTF8ToString(path_ptr);
    
    // Read from Emscripten's virtual filesystem (preloaded files)
    try {
        var data = FS.readFile(path);
        var arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        window.gameAudio.ctx.decodeAudioData(arrayBuffer)
            .then(function(buffer) {
                window.gameAudio.bgmBuffer = buffer;
                console.log('Audio loaded from FS: ' + path);
                // Auto-play if play was requested before buffer was ready
                if (window.gameAudio.pendingBgmPlay) {
                    window.gameAudio.pendingBgmPlay = false;
                    if (window.gameAudio.ctx.state === 'suspended') {
                        window.gameAudio.ctx.resume();
                    }
                    window.gameAudio.bgmSource = window.gameAudio.ctx.createBufferSource();
                    window.gameAudio.bgmSource.buffer = window.gameAudio.bgmBuffer;
                    window.gameAudio.bgmSource.loop = true;
                    window.gameAudio.bgmSource.connect(window.gameAudio.bgmGain);
                    window.gameAudio.bgmSource.start(0);
                    console.log('BGM auto-started after load');
                }
            })
            .catch(function(err) { console.error('Failed to decode audio:', err); });
    } catch (e) {
        console.error('Failed to read audio file from FS:', path, e);
    }
});

EM_JS(void, web_audio_play_bgm_internal, (), {
    if (!window.gameAudio) return;
    
    // If buffer not ready yet, mark as pending and return
    if (!window.gameAudio.bgmBuffer) {
        window.gameAudio.pendingBgmPlay = true;
        console.log('BGM play requested, waiting for buffer...');
        return;
    }
    
    if (window.gameAudio.ctx.state === 'suspended') {
        window.gameAudio.ctx.resume();
    }
    
    if (window.gameAudio.bgmSource) {
        try { window.gameAudio.bgmSource.stop(); } catch(e) {}
    }
    
    window.gameAudio.bgmSource = window.gameAudio.ctx.createBufferSource();
    window.gameAudio.bgmSource.buffer = window.gameAudio.bgmBuffer;
    window.gameAudio.bgmSource.loop = true;
    window.gameAudio.bgmSource.connect(window.gameAudio.bgmGain);
    window.gameAudio.bgmSource.start(0);
    console.log('BGM started playing');
});

EM_JS(void, web_audio_stop_bgm_internal, (), {
    if (!window.gameAudio || !window.gameAudio.bgmSource) return;
    try { window.gameAudio.bgmSource.stop(); } catch(e) {}
    window.gameAudio.bgmSource = null;
});

EM_JS(void, web_audio_set_muted_internal, (int muted), {
    if (!window.gameAudio) return;
    window.gameAudio.muted = !!muted;
    if (window.gameAudio.bgmGain) {
        window.gameAudio.bgmGain.gain.value = muted ? 0.0 : window.gameAudio.volume;
    }
});

EM_JS(void, web_audio_set_volume_internal, (float vol), {
    if (!window.gameAudio) return;
    window.gameAudio.volume = vol;
    if (window.gameAudio.bgmGain && !window.gameAudio.muted) {
        window.gameAudio.bgmGain.gain.value = vol;
    }
});

EM_JS(void, web_audio_play_sfx_internal, (const char* path_ptr), {
    if (!window.gameAudio || !window.gameAudio.ctx || window.gameAudio.muted) return;
    
    var path = UTF8ToString(path_ptr);
    
    // Check cache first
    if (window.gameAudio.sfxBuffers[path]) {
        var source = window.gameAudio.ctx.createBufferSource();
        source.buffer = window.gameAudio.sfxBuffers[path];
        var gain = window.gameAudio.ctx.createGain();
        gain.gain.value = window.gameAudio.volume;
        source.connect(gain);
        gain.connect(window.gameAudio.ctx.destination);
        source.start(0);
        return;
    }
    
    // Read from Emscripten's virtual filesystem
    try {
        var data = FS.readFile(path);
        var arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        window.gameAudio.ctx.decodeAudioData(arrayBuffer)
            .then(function(buffer) {
                window.gameAudio.sfxBuffers[path] = buffer;
                var source = window.gameAudio.ctx.createBufferSource();
                source.buffer = buffer;
                var gain = window.gameAudio.ctx.createGain();
                gain.gain.value = window.gameAudio.volume;
                source.connect(gain);
                gain.connect(window.gameAudio.ctx.destination);
                source.start(0);
            })
            .catch(function(err) { console.error('Failed to decode SFX:', err); });
    } catch (e) {
        console.error('Failed to read SFX file from FS:', path, e);
    }
});

EM_JS(void, web_audio_preload_sfx_internal, (const char* path_ptr), {
    if (!window.gameAudio || !window.gameAudio.ctx) return;
    
    var path = UTF8ToString(path_ptr);
    if (window.gameAudio.sfxBuffers[path]) return;
    
    // Read from Emscripten's virtual filesystem
    try {
        var data = FS.readFile(path);
        var arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        window.gameAudio.ctx.decodeAudioData(arrayBuffer)
            .then(function(buffer) {
                window.gameAudio.sfxBuffers[path] = buffer;
                console.log('SFX preloaded from FS: ' + path);
            })
            .catch(function(err) { console.error('Failed to decode SFX:', err); });
    } catch (e) {
        console.error('Failed to read SFX file from FS:', path, e);
    }
});

EM_JS(void, web_audio_shutdown_internal, (), {
    if (!window.gameAudio) return;
    if (window.gameAudio.bgmSource) {
        try { window.gameAudio.bgmSource.stop(); } catch(e) {}
    }
    if (window.gameAudio.ctx) {
        window.gameAudio.ctx.close();
    }
    window.gameAudio = null;
});

// Public C++ wrapper functions that can be called from other translation units
extern "C" {

void js_audio_init() {
    web_audio_init_internal();
}

void js_audio_load_bgm(const char* path_ptr) {
    web_audio_load_bgm_internal(path_ptr);
}

void js_audio_play_bgm() {
    web_audio_play_bgm_internal();
}

void js_audio_stop_bgm() {
    web_audio_stop_bgm_internal();
}

void js_audio_set_muted(int muted) {
    web_audio_set_muted_internal(muted);
}

void js_audio_set_volume(float vol) {
    web_audio_set_volume_internal(vol);
}

void js_audio_play_sfx(const char* path_ptr) {
    web_audio_play_sfx_internal(path_ptr);
}

void js_audio_preload_sfx(const char* path_ptr) {
    web_audio_preload_sfx_internal(path_ptr);
}

void js_audio_shutdown() {
    web_audio_shutdown_internal();
}

} // extern "C"

#endif // __EMSCRIPTEN__
