import type {Direction, AnimationState} from './types';
import {ANIMATION_FRAME_DELAYS} from './animation-constants';

export const AnimationManager = {
	getFrameDelays(animationType: string): number[] {
		return ANIMATION_FRAME_DELAYS[animationType] ?? [];
	},

	createAnimationState(): AnimationState {
		return {
			currentAnimation: 'idle',
			currentDirection: 'front',
			currentFrame: 0,
			frameTimer: 0,
			isPlaying: true,
		};
	},

	updateAnimation(state: AnimationState, deltaTime: number, loop = true): AnimationState {
		if (!state.isPlaying) {
			return state;
		}

		const delays = this.getFrameDelays(state.currentAnimation);
		const frameDelay = delays[state.currentFrame] ?? delays[0] ?? 0;

		state.frameTimer += deltaTime;

		if (state.frameTimer >= frameDelay) {
			if (!loop && state.currentFrame === delays.length - 1) {
				// Clamp at last frame so completion can be detected
				state.frameTimer = frameDelay;
			} else {
				state.frameTimer = 0;
				state.currentFrame = (state.currentFrame + 1) % delays.length;
			}
		}

		return state;
	},

	setAnimation(state: AnimationState, animationType: string): AnimationState {
		if (state.currentAnimation === animationType) {
			return state;
		}

		state.currentAnimation = animationType;
		state.currentFrame = 0;
		state.frameTimer = 0;
		return state;
	},

	setDirection(state: AnimationState, direction: Direction): AnimationState {
		if (state.currentDirection === direction) {
			return state;
		}

		// Keep frame and timer — direction change should not interrupt the walk cycle
		state.currentDirection = direction;
		return state;
	},

	isAnimationComplete(state: AnimationState): boolean {
		const delays = this.getFrameDelays(state.currentAnimation);
		return state.currentFrame === delays.length - 1 && state.frameTimer >= (delays[state.currentFrame] ?? 0);
	},

	resetAnimation(state: AnimationState): AnimationState {
		state.currentFrame = 0;
		state.frameTimer = 0;
		return state;
	},
};
