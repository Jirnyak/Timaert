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

		const newState = {...state};
		newState.frameTimer += deltaTime;

		if (newState.frameTimer >= frameDelay) {
			if (!loop && newState.currentFrame === delays.length - 1) {
				// Clamp at last frame so completion can be detected
				newState.frameTimer = frameDelay;
			} else {
				newState.frameTimer = 0;
				newState.currentFrame = (newState.currentFrame + 1) % delays.length;
			}
		}

		return newState;
	},

	setAnimation(state: AnimationState, animationType: string): AnimationState {
		if (state.currentAnimation === animationType) {
			return state;
		}

		return {
			...state,
			currentAnimation: animationType,
			currentFrame: 0,
			frameTimer: 0,
		};
	},

	setDirection(state: AnimationState, direction: Direction): AnimationState {
		if (state.currentDirection === direction) {
			return state;
		}

		// Keep frame and timer — direction change should not interrupt the walk cycle
		return {
			...state,
			currentDirection: direction,
		};
	},

	isAnimationComplete(state: AnimationState): boolean {
		const delays = this.getFrameDelays(state.currentAnimation);
		return state.currentFrame === delays.length - 1 && state.frameTimer >= (delays[state.currentFrame] ?? 0);
	},

	resetAnimation(state: AnimationState): AnimationState {
		return {
			...state,
			currentFrame: 0,
			frameTimer: 0,
		};
	},
};
