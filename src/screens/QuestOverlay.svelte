<script lang="ts">
	import type {PlayerState} from '../game/state';
	import {
		color, panelStyle, dividerStyle, accentHeadingStyle, bodyStyle, mutedStyle, btnProps, tabStyle,
	} from '../ui/theme';

	type Props = {
		player: PlayerState;
		onClose: () => void;
		onAbandon: (questId: string) => void;
	};

	const {player, onClose, onAbandon}: Props = $props();

	let selectedQuestId = $state<string | undefined>(undefined);
	const selectedQuest = $derived(player.activeQuests.find(q => q.id === selectedQuestId) ?? player.activeQuests[0]);
</script>

<svelte:window onkeydown={event => {
	if (event.key === 'Escape' || event.key === 'q' || event.key === 'Q') {
		onClose();
	}
}} />

<div class="absolute inset-0 flex items-center justify-center z-100" style="background: {color.backdrop};">
	<div class="w-[560px] rounded-lg border-4 p-5 font-sans overflow-hidden" style={panelStyle()}>
		<!-- Header -->
		<div class="mb-4 flex items-center justify-between border-b pb-3" style={dividerStyle}>
			<h2 class="text-xl font-black tracking-tight uppercase" style={accentHeadingStyle}>Quest Journal</h2>
			<div class="flex items-center gap-3">
				<span class="text-xs" style={mutedStyle}>Active: {player.activeQuests.length} · Done: {player.completedQuestIds.length}</span>
				<button onclick={onClose} class="rounded border-2 px-2 py-1 text-[10px] font-bold uppercase tracking-tighter transition" {...btnProps('close')}>Close [Q]</button>
			</div>
		</div>

		{#if player.activeQuests.length === 0}
			<p class="py-8 text-center text-sm" style={mutedStyle}>No active quests. Visit a settlement to find work.</p>
		{:else}
			<div class="flex gap-3" style="min-height: 240px;">
				<!-- Quest list -->
				<div class="w-40 shrink-0 space-y-1 overflow-y-auto max-h-64">
					{#each player.activeQuests as quest (quest.id)}
						<button
							onclick={() => {
								selectedQuestId = quest.id;
							}}
							class="w-full rounded px-2 py-1.5 text-left text-xs transition"
							style={tabStyle(selectedQuestId === quest.id)}
						>
							<div style="color: {color.heading}; font-weight: bold;">{quest.title}</div>
							<div class="text-[10px] uppercase tracking-widest" style="color: {quest.category === 'main' ? color.accent : color.muted};">{quest.category}</div>
						</button>
					{/each}
				</div>

				<!-- Quest detail -->
				<div class="flex-1 rounded border p-3" style="border-color: {color.divider}; background: {color.darkBg};">
					{#if selectedQuest}
						<h3 class="text-sm font-bold" style="color: {color.heading};">{selectedQuest.title}</h3>
						<p class="mt-2 text-xs leading-relaxed" style={bodyStyle}>{selectedQuest.description}</p>

						<!-- Objectives -->
						<div class="mt-3 space-y-1">
							<div class="text-[10px] font-bold uppercase tracking-widest" style={mutedStyle}>Objectives</div>
							{#each selectedQuest.objectives as obj}
								<div class="flex items-center gap-2 text-xs" style={bodyStyle}>
									<span style="color: {obj.completed ? color.positive : color.muted};">{obj.completed ? '✓' : '○'}</span>
									<span>
										{#if obj.type === 'visit_cell'}Go to ({obj.x}, {obj.y}){/if}
										{#if obj.type === 'find_location'}Find location at ({obj.cellX}, {obj.cellY}){/if}
										{#if obj.type === 'deliver_items'}Deliver {obj.quantity}× {obj.itemId.replace('mat_', '')}{/if}
										{#if obj.type === 'destroy_npc'}Eliminate targets ({obj.killed}/{obj.count}){/if}
										{#if obj.type === 'wait_at'}Defend position ({obj.hoursWaited}/{obj.hoursRequired}h){/if}
										{#if obj.type === 'interact_cell'}Interact at ({obj.x}, {obj.y}){/if}
									</span>
								</div>
							{/each}
						</div>

						<!-- Rewards -->
						<div class="mt-3 space-y-1">
							<div class="text-[10px] font-bold uppercase tracking-widest" style={mutedStyle}>Rewards</div>
							<div class="flex flex-wrap gap-2 text-xs">
								{#each selectedQuest.rewards as reward}
									{#if reward.type === 'gold'}
										<span style="color: {color.accent};">{reward.amount}g</span>
									{/if}
									{#if reward.type === 'xp'}
										<span style="color: {color.positive};">+{reward.amount} XP</span>
									{/if}
									{#if reward.type === 'reputation'}
										<span style="color: {color.mp};">+{reward.delta} {reward.faction} rep</span>
									{/if}
									{#if reward.type === 'item'}
										<span style={bodyStyle}>{reward.quantity}× {reward.itemId}</span>
									{/if}
								{/each}
							</div>
						</div>

						{#if selectedQuest.expireDay}
							<div class="mt-2 text-[10px]" style={mutedStyle}>Expires: day {selectedQuest.expireDay}</div>
						{/if}

						<div class="mt-3 pt-2 border-t" style={dividerStyle}>
							<button
								onclick={() => onAbandon(selectedQuest!.id)}
								class="rounded border-2 px-3 py-1 text-xs font-bold transition"
								{...btnProps('close')}
							>Abandon Quest</button>
						</div>
					{:else}
						<p class="text-xs" style={mutedStyle}>Select a quest from the list.</p>
					{/if}
				</div>
			</div>
		{/if}
	</div>
</div>
