<script lang="ts">
	import type {ShowStoryEvent, StoryPhase} from '../game/event-types';
	import type {StoryResult} from '../game/plot';
	import {color, panelStyle, dividerStyle, accentHeadingStyle, bodyStyle, btnProps} from '../ui/theme';

	type Props = {
		story: ShowStoryEvent;
		onComplete: (result: StoryResult) => void;
	};

	let {story, onComplete}: Props = $props();

	// ── State machine ──

	let phaseIndex = $state(0);
	let slideIndex = $state(0);
	let choices = $state<StoryResult>({});
	let fadeIn = $state(true);

	let phase = $derived(story.phases[phaseIndex] as StoryPhase | undefined);
	let hasPortraits = $derived(
		phase?.type === 'choice' && phase.options.some(o => o.image),
	);

	// ── Navigation ──

	function fade(next: () => void) {
		fadeIn = false;
		setTimeout(() => {
			next();
			fadeIn = true;
		}, 300);
	}

	function advanceSlide() {
		if (!phase || phase.type !== 'slides') return;
		if (slideIndex < phase.slides.length - 1) {
			fade(() => {
				slideIndex++;
			});
		} else {
			nextPhase();
		}
	}

	function selectChoice(value: string) {
		if (!phase || phase.type !== 'choice') return;
		choices[phase.id] = value;
		nextPhase();
	}

	function nextPhase() {
		if (phaseIndex < story.phases.length - 1) {
			fade(() => {
				phaseIndex++;
				slideIndex = 0;
			});
		} else {
			onComplete(choices);
		}
	}

	function handleKeydown(e: KeyboardEvent) {
		if (e.key === 'Enter' || e.key === ' ') {
			e.preventDefault();
			if (phase?.type === 'slides') advanceSlide();
		}
	}
</script>

<svelte:window onkeydown={handleKeydown} />

<div
	class="absolute inset-0 z-50 flex items-center justify-center"
	style="background: {color.backdropHeavy};"
>
	{#if phase?.type === 'slides'}
		<!-- ─── Slide phase ─── -->
		{@const slide = phase.slides[slideIndex]}
		<div
			class="absolute inset-0 bg-cover bg-center transition-opacity duration-500"
			style="background-image: url('{slide.image}'); opacity: {fadeIn ? 1 : 0};"
		></div>

		<div class="absolute inset-x-0 bottom-0 flex flex-col items-center gap-4 pb-12">
			{#if slide.narration}
				<div
					class="mx-auto max-w-2xl rounded-lg border-2 px-8 py-4 text-center font-sans text-lg leading-relaxed transition-opacity duration-500"
					style="{panelStyle()}; opacity: {fadeIn ? 0.95 : 0};"
				>
					<p style={bodyStyle}>{slide.narration}</p>
				</div>
			{/if}
			<button
				onclick={advanceSlide}
				class="rounded border-2 px-8 py-3 font-sans text-sm font-bold transition"
				{...btnProps('title')}
			>{slideIndex < phase.slides.length - 1 ? 'Continue' : 'Begin'}</button>
			<span class="font-sans text-xs" style="color: {color.muted};">
				{slideIndex + 1} / {phase.slides.length}
			</span>
		</div>

	{:else if phase?.type === 'choice' && hasPortraits}
		<!-- ─── Portrait choice (e.g. sex selection) ─── -->
		<div
			class="flex w-[680px] flex-col items-center rounded-lg border-4 font-sans transition-opacity duration-500"
			style="{panelStyle('large')}; opacity: {fadeIn ? 1 : 0};"
		>
			<div class="w-full border-b px-6 py-4" style={dividerStyle}>
				<h2 class="text-center text-xl font-black" style={accentHeadingStyle}>{phase.title}</h2>
				<p class="mt-1 text-center text-sm leading-relaxed" style={bodyStyle}>{phase.description}</p>
			</div>

			<div class="flex w-full justify-center gap-6 p-6">
				{#each phase.options as option}
					<button
						onclick={() => selectChoice(option.value)}
						class="group flex w-[260px] flex-col items-center rounded-lg border-2 p-4 transition"
						{...btnProps('secondary')}
					>
						{#if option.image}
							<img
								src={option.image}
								alt={option.label}
								class="mb-3 h-48 w-auto rounded object-contain"
							/>
						{/if}
						<span class="text-lg font-bold" style="color: {color.heading};">{option.label}</span>
						<span class="mt-1 text-xs" style="color: {color.muted};">{option.description}</span>
					</button>
				{/each}
			</div>
		</div>

	{:else if phase?.type === 'choice'}
		<!-- ─── Text-only choice (e.g. realm selection) ─── -->
		<div
			class="flex w-[520px] flex-col rounded-lg border-4 font-sans transition-opacity duration-500"
			style="{panelStyle('large')}; opacity: {fadeIn ? 1 : 0};"
		>
			<div class="border-b px-6 py-4" style={dividerStyle}>
				<h2 class="text-xl font-black" style={accentHeadingStyle}>{phase.title}</h2>
			</div>
			<div class="border-b px-6 py-3" style={dividerStyle}>
				<p class="text-sm leading-relaxed" style={bodyStyle}>{phase.description}</p>
			</div>
			<div class="flex flex-col gap-3 p-5">
				{#each phase.options as option}
					<button
						onclick={() => selectChoice(option.value)}
						class="flex flex-col items-start rounded border-2 px-5 py-4 text-left transition"
						{...btnProps('secondary')}
					>
						<span class="text-base font-bold" style="color: {color.heading};">{option.label}</span>
						<span class="mt-1 text-xs" style="color: {color.muted};">{option.description}</span>
					</button>
				{/each}
			</div>
		</div>
	{/if}
</div>
