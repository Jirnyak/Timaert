// Web Audio BGM manager — defers AudioContext creation until first user gesture

let ctx: AudioContext | undefined;
let gainNode: GainNode | undefined;
let bgmSource: AudioBufferSourceNode | undefined;
let bgmBuffer: AudioBuffer | undefined;
let rawArrayBuffer: ArrayBuffer | undefined;
let muted = false;
let volume = 0.4;
let wantPlay = false;

function ensureContext(): AudioContext {
	if (!ctx) {
		ctx = new AudioContext();
		gainNode = ctx.createGain();
		gainNode.gain.value = muted ? 0 : volume;
		gainNode.connect(ctx.destination);
	}

	return ctx;
}

// Fetch audio data without creating AudioContext (safe before gesture)
export async function loadBgm(url: string): Promise<void> {
	const response = await fetch(url);
	rawArrayBuffer = await response.arrayBuffer();
}

// Decode the fetched buffer using AudioContext (requires gesture)
async function decodeIfNeeded(): Promise<boolean> {
	if (bgmBuffer) {
		return true;
	}

	if (!rawArrayBuffer) {
		return false;
	}

	const audioCtx = ensureContext();
	// ArrayBuffer can only be decoded once; clone it
	bgmBuffer = await audioCtx.decodeAudioData(rawArrayBuffer.slice(0)); // eslint-disable-line unicorn/prefer-spread
	return true;
}

export async function playBgm(): Promise<void> {
	wantPlay = true;
	const ready = await decodeIfNeeded();
	if (!ready || !bgmBuffer || !gainNode) {
		return;
	}

	const audioCtx = ensureContext();
	if (audioCtx.state === 'suspended') {
		void audioCtx.resume();
	}

	stopBgm();
	bgmSource = audioCtx.createBufferSource();
	bgmSource.buffer = bgmBuffer;
	bgmSource.loop = true;
	bgmSource.connect(gainNode);
	bgmSource.start(0);
}

export function stopBgm(): void {
	if (bgmSource) {
		try {
			bgmSource.stop();
		} catch {
			// Already stopped
		}

		bgmSource = undefined;
	}
}

export function toggleMute(): boolean {
	muted = !muted;
	if (gainNode) {
		gainNode.gain.value = muted ? 0 : volume;
	}

	// If music was waiting to play, start it now on this user gesture
	if (!muted && wantPlay && !bgmSource) {
		void playBgm();
	}

	return muted;
}

export function setVolume(v: number): void {
	volume = Math.max(0, Math.min(1, v));
	if (gainNode && !muted) {
		gainNode.gain.value = volume;
	}
}

export function isMuted(): boolean {
	return muted;
}

// Call on any user gesture to ensure audio can play
export function resumeOnInteraction(): void {
	if (ctx?.state === 'suspended') {
		void ctx.resume();
	}

	// If music was loaded but never played due to autoplay policy, start it now
	if (wantPlay && !bgmSource) {
		void playBgm();
	}
}
