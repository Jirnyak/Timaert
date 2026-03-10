<script lang="ts">
	import {listSaves} from '../game/state';
	import {
		color, backdropStyle, panelStyle, headingStyle, btnStyle, btnHover, btnOut, bodyStyle,
	} from '../ui/theme';

	type Props = {
		onResume: () => void;
		onSave: () => void;
		onLoad: (key: string) => void;
		onCodex: () => void;
		onToTitle: () => void;
	};

	const {onResume, onSave, onLoad, onCodex, onToTitle}: Props = $props();

	let showLoadList = $state(false);
	let saves = $state(listSaves());

	// RPG Awesome icon codepoints
	const RA_FORWARD = '\uE9D4';
	const RA_SAVE = '\uEA8D';
	const RA_LOAD = '\uEA34';
	const RA_CASTLE = '\uE95D';
	const RA_BOOK = '\uE92B';

	function handleSave() {
		onSave();
		saves = listSaves();
	}

	function toggleLoad() {
		showLoadList = !showLoadList;
		if (showLoadList) {
			saves = listSaves();
		}
	}

	const menuBtnClass = 'flex items-center gap-4 rounded border-2 font-sans text-lg font-bold transition';
	const menuExtra = 'padding: 12px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.2); text-shadow: 0 1px 1px rgba(255,255,255,0.3);';
</script>

<div class="absolute inset-0 flex items-center justify-center" style={backdropStyle()}>
	<div class="flex w-96 flex-col gap-3 rounded-lg border-4 p-6" style={panelStyle()}>
		<h2 class="mb-2 text-center font-sans text-3xl font-black tracking-wide" style={headingStyle}>PAUSED</h2>

		<button onclick={onResume} class={menuBtnClass} style="{btnStyle('menu')} {menuExtra}" onmouseover={btnHover('menu')} onmouseout={btnOut('menu')} onfocus={btnHover('menu')} onblur={btnOut('menu')}>
			<span class="ra-icon text-2xl" style="color: #5a8a5a; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_FORWARD}</span>
			Resume
		</button>

		<button onclick={handleSave} class={menuBtnClass} style="{btnStyle('menu')} {menuExtra}" onmouseover={btnHover('menu')} onmouseout={btnOut('menu')} onfocus={btnHover('menu')} onblur={btnOut('menu')}>
			<span class="ra-icon text-2xl" style="color: {color.positive}; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_SAVE}</span>
			Save
		</button>

		<button onclick={toggleLoad} class={menuBtnClass} style="{btnStyle('menu')} {menuExtra}" onmouseover={btnHover('menu')} onmouseout={btnOut('menu')} onfocus={btnHover('menu')} onblur={btnOut('menu')}>
			<span class="ra-icon text-2xl" style="color: {color.warning}; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_LOAD}</span>
			Load
		</button>

		<button onclick={onCodex} class={menuBtnClass} style="{btnStyle('menu')} {menuExtra}" onmouseover={btnHover('menu')} onmouseout={btnOut('menu')} onfocus={btnHover('menu')} onblur={btnOut('menu')}>
			<span class="ra-icon text-2xl" style="color: {color.mp}; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_BOOK}</span>
			Codex
		</button>

		{#if showLoadList}
			<div class="max-h-48 overflow-y-auto rounded border-2 p-2" style="background: {color.messageBg}; border-color: #5c422e;">
				{#if saves.length === 0}
					<p class="py-2 text-center font-sans text-sm" style={bodyStyle}>No saves found</p>
				{:else}
					{#each saves as save (save.key)}
						<button
							onclick={() => onLoad(save.key)}
							class="mb-1 block w-full rounded border p-2 text-left font-sans text-sm transition"
							style="background: {color.cardBg}; border-color: {color.divider}; color: {color.heading};"
							onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)'}
							onmouseout={e => e.currentTarget.style.background = color.cardBg}
							onfocus={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)'}
							onblur={e => e.currentTarget.style.background = color.cardBg}
						>
							<span class="font-bold">{save.name}</span>
							<span class="ml-2 text-xs" style="color: {color.subtitle};">{new Date(save.savedAt).toLocaleString()}</span>
						</button>
					{/each}
				{/if}
			</div>
		{/if}

		<button onclick={onToTitle} class={menuBtnClass} style="{btnStyle('menu')} {menuExtra}" onmouseover={btnHover('menu')} onmouseout={btnOut('menu')} onfocus={btnHover('menu')} onblur={btnOut('menu')}>
			<span class="ra-icon text-2xl" style="color: {color.warning}; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_CASTLE}</span>
			To main menu
		</button>
	</div>
</div>

<style>
	.ra-icon {
		font-family: 'RPG Awesome', sans-serif;
	}
</style>
