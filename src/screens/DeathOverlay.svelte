<script lang="ts">
	import {
		color, panelStyle, btnProps, dividerStyle,
	} from '../ui/theme';

	type Props = {
		onRestart: () => void;
		onReborn: () => void;
		onMainMenu: () => void;
	};

	const {onRestart, onReborn, onMainMenu}: Props = $props();
</script>

<div
	class="absolute inset-0 flex items-center justify-center"
	style="background: {color.backdropHeavy};"
>
	<div
		class="flex w-80 flex-col rounded-lg border-4 font-sans"
		style={panelStyle('large')}
	>
		<div class="border-b p-6 text-center" style={dividerStyle}>
			<h2 class="text-3xl font-bold" style="color: {color.hp}; text-shadow: 0 2px 4px rgba(0,0,0,0.3);">
				You Died
			</h2>
		</div>

		<div class="flex flex-col gap-3 p-6">
			<button
				class="rounded border-2 px-4 py-3 font-sans text-sm font-semibold"
				{...btnProps('primary')}
				onclick={onRestart}
			>
				Restart
			</button>

			<button
				class="rounded border-2 px-4 py-3 font-sans text-sm font-semibold"
				{...btnProps('action')}
				onclick={onReborn}
			>
				Reborn as Another Person
			</button>

			<button
				class="rounded border-2 px-4 py-3 font-sans text-sm font-semibold"
				{...btnProps('muted')}
				onclick={onMainMenu}
			>
				Main Menu
			</button>
		</div>
	</div>
</div>
