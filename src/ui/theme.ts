/**
 * Unified medieval parchment UI design system.
 *
 * Every overlay imports tokens and helpers from here instead of
 * hard-coding hex values.  If a single screen needs a tweak it may
 * override, but the defaults live here — single source of truth.
 */

// ── Color Tokens ────────────────────────────────────────────────

export const color = {
	// Backdrops
	backdrop: 'rgba(20, 10, 5, 0.85)',
	backdropMedium: 'rgba(20, 10, 5, 0.9)',
	backdropHeavy: 'rgba(20, 10, 5, 0.95)',

	// Panel
	panelBg: 'linear-gradient(to bottom, #e8d4b8, #d4bf9f)',
	panelBorder: '#6b4f3a',
	panelShadow: '0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3)',
	panelShadowLg: '0 8px 24px rgba(0,0,0,0.8), inset 0 2px 0 rgba(255,255,255,0.3)',

	// Divider / thin borders
	divider: '#8b6f47',

	// Text hierarchy
	heading: '#3d2817',
	accent: '#8b6f3a',
	body: '#5a4a3a',
	muted: '#7a6a5a',
	label: '#5a3a2a',
	light: '#f0e8d8',
	subtitle: '#6a5a4a',
	headingShadow: '0 1px 2px rgba(255,255,255,0.5)',

	// Semantic resource colors
	hp: '#8b3a3a',
	mp: '#3a5a8b',
	sp: '#8b6f3a',
	positive: '#4a7c4a',
	negative: '#8a3a3a',
	warning: '#b8935a',

	// Bars
	barTrack: '#5a3a2a',
	barBorder: '#3d2817',
	hpFill: 'linear-gradient(to right, #c84a4a, #d86a6a)',
	enemyHpFill: 'linear-gradient(to right, #d4a574, #e4b584)',

	// Message box
	messageBg: 'linear-gradient(to bottom, #b8a890, #a89880)',
	messageBorder: '#6b4f3a',

	// Miscellaneous backgrounds
	darkBg: '#1a1410',
	pageGradient: 'linear-gradient(to bottom, #2a1810, #1a0f08)',
	sidebarBg: 'linear-gradient(to right, #c8b89f, #d4bf9f)',
	contentBg: 'linear-gradient(to bottom, #f4e8d4, #e8d4b8)',
	innerPanelBg: 'linear-gradient(to bottom, #c8b89f, #b8a88f)',
	inputBg: 'linear-gradient(to bottom, #f0e8d8, #e0d8c8)',
	cardBg: 'linear-gradient(to bottom, #d4bf9f, #c4af8f)',
	emptySlotBorder: '#9a8570',
	emptySlotBg: 'linear-gradient(to bottom, #a89880, #988870)',
} as const;

// ── Button Variants ─────────────────────────────────────────────

type ButtonVariant = {
	bg: string;
	hoverBg: string;
	border: string;
	text: string;
};

export const buttonVariants = {
	/** Gold/amber – "Enter City", "Trade", "Loot", "Level Up" */
	primary: {
		bg: 'linear-gradient(to bottom, #d4a574, #b8935a)',
		hoverBg: 'linear-gradient(to bottom, #e4b584, #c8a36a)',
		border: '#8b6f47',
		text: '#3d2817',
	},
	/** Neutral parchment – dialog choices, "Continue" */
	secondary: {
		bg: 'linear-gradient(to bottom, #c8b89f, #b8a88f)',
		hoverBg: 'linear-gradient(to bottom, #d8c8af, #c8b89f)',
		border: '#8b6f47',
		text: '#3d2817',
	},
	/** Close / dismiss – "Close [Esc]", message boxes */
	close: {
		bg: 'linear-gradient(to bottom, #b8a890, #a89880)',
		hoverBg: 'linear-gradient(to bottom, #c8b8a0, #b8a890)',
		border: '#6b4f3a',
		text: '#3d2817',
	},
	/** Muted – "Leave [Esc]", "Back" */
	muted: {
		bg: 'linear-gradient(to bottom, #a89880, #988870)',
		hoverBg: 'linear-gradient(to bottom, #b8a890, #a89880)',
		border: '#7a6a5a',
		text: '#3d2817',
	},
	/** Blue-steel CTA – "Rest", "Talk", "Regenerate", stat + buttons */
	action: {
		bg: 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)',
		hoverBg: 'linear-gradient(to bottom, #9aaaba, #7a8a9a)',
		border: '#5a6a7a',
		text: '#f0e8d8',
	},
	/** Danger / fight – red */
	danger: {
		bg: 'linear-gradient(to bottom, #c86a6a, #a84a4a)',
		hoverBg: 'linear-gradient(to bottom, #d87a7a, #b85a5a)',
		border: '#8a3a3a',
		text: '#f0e8d8',
	},
	/** Success / mercy – green */
	success: {
		bg: 'linear-gradient(to bottom, #8aaa8a, #6a8a6a)',
		hoverBg: 'linear-gradient(to bottom, #9aba9a, #7a9a7a)',
		border: '#5a7a5a',
		text: '#2a3a2a',
	},
	/** Mock / tease – purple */
	purple: {
		bg: 'linear-gradient(to bottom, #aa9aba, #8a7a9a)',
		hoverBg: 'linear-gradient(to bottom, #baaaca, #9a8aaa)',
		border: '#7a6a8a',
		text: '#3a2a4a',
	},
	/** Run / escape – earthy brown */
	escape: {
		bg: 'linear-gradient(to bottom, #9a8a7a, #7a6a5a)',
		hoverBg: 'linear-gradient(to bottom, #aa9a8a, #8a7a6a)',
		border: '#6a5a4a',
		text: '#2a1a0a',
	},
	/** Pause-menu buttons */
	menu: {
		bg: 'linear-gradient(to bottom, #c8b89f, #b0a080)',
		hoverBg: 'linear-gradient(to bottom, #d8c8af, #c0b090)',
		border: '#6b4f3a',
		text: '#3d2817',
	},
	/** Title-screen CTA */
	title: {
		bg: 'linear-gradient(to bottom, #e8d4b8, #d4bf9f)',
		hoverBg: 'linear-gradient(to bottom, #f0dcc5, #dcc7a7)',
		border: '#8b6f47',
		text: '#3d2817',
	},
	/** Perk selection purple */
	perk: {
		bg: 'linear-gradient(to bottom, #9a7a9a, #7a5a7a)',
		hoverBg: 'linear-gradient(to bottom, #aa8aaa, #8a6a8a)',
		border: '#5a3a5a',
		text: '#f0e8d8',
	},
	/** Battle combat buttons (Punch / Wait) – lighter steel */
	combat: {
		bg: 'linear-gradient(to bottom, #9aaaba, #7a8a9a)',
		hoverBg: 'linear-gradient(to bottom, #aabaca, #8a9aaa)',
		border: '#6a7a8a',
		text: '#2a3a4a',
	},
} as const satisfies Record<string, ButtonVariant>;

export type ButtonVariantName = keyof typeof buttonVariants;

// ── Style String Helpers ────────────────────────────────────────

/** Backdrop / dimmer behind a panel. */
export function backdropStyle(opacity: 'light' | 'medium' | 'heavy' = 'medium'): string {
	const map = {light: color.backdrop, medium: color.backdropMedium, heavy: color.backdropHeavy};
	return `background: ${map[opacity]};`;
}

/** Main panel container. */
export function panelStyle(shadow: 'default' | 'large' = 'default'): string {
	return `background: ${color.panelBg}; border-color: ${color.panelBorder}; box-shadow: ${shadow === 'large' ? color.panelShadowLg : color.panelShadow};`;
}

/** Divider / section border. */
export const dividerStyle = `border-color: ${color.divider};`;

/** Dark heading text with light text-shadow. */
export const headingStyle = `color: ${color.heading}; text-shadow: ${color.headingShadow};`;

/** Golden accent heading. */
export const accentHeadingStyle = `color: ${color.accent}; text-shadow: ${color.headingShadow};`;

/** Body text. */
export const bodyStyle = `color: ${color.body};`;

/** Muted / secondary text. */
export const mutedStyle = `color: ${color.muted};`;

/** Section header (bordered + body color). */
export const sectionStyle = `border-color: ${color.divider}; color: ${color.body};`;

/** Notification / message bar. */
export const messageStyle = `background: ${color.messageBg}; border-color: ${color.messageBorder}; color: ${color.heading};`;

// ── Button Helpers ──────────────────────────────────────────────

/** Inline style string for a button variant. */
export function btnStyle(variant: ButtonVariantName): string {
	const v = buttonVariants[variant];
	return `background: ${v.bg}; border-color: ${v.border}; color: ${v.text};`;
}

/** Mouseover / focus handler — swaps to hover gradient. */
export function btnHover(variant: ButtonVariantName): (event: Event) => void {
	const {hoverBg} = buttonVariants[variant];
	return (event: Event) => {
		(event.currentTarget as HTMLElement).style.background = hoverBg;
	};
}

/** Mouseout / blur handler — restores default gradient. */
export function btnOut(variant: ButtonVariantName): (event: Event) => void {
	const {bg} = buttonVariants[variant];
	return (event: Event) => {
		(event.currentTarget as HTMLElement).style.background = bg;
	};
}

/**
 * Spread-friendly button properties (style + hover/out handlers).
 *
 * Usage: `<button {...btnProps('primary')}>Click</button>`
 */
export function btnProps(variant: ButtonVariantName) {
	return {
		style: btnStyle(variant),
		onmouseover: btnHover(variant),
		onmouseout: btnOut(variant),
	};
}

/**
 * Conditional button props — hover only fires when `enabled` is true.
 * Handy for battle buttons that can be disabled.
 */
export function btnPropsIf(variant: ButtonVariantName, enabled: boolean) {
	const v = buttonVariants[variant];
	return {
		style: btnStyle(variant),
		onmouseover(event: MouseEvent) {
			if (enabled) {
				(event.currentTarget as HTMLElement).style.background = v.hoverBg;
			}
		},
		onmouseout(event: MouseEvent) {
			if (enabled) {
				(event.currentTarget as HTMLElement).style.background = v.bg;
			}
		},
	};
}

// ── Tab Helpers ─────────────────────────────────────────────────

/** Tab button inline style (active vs inactive). */
export function tabStyle(isActive: boolean): string {
	if (isActive) {
		return `background: ${buttonVariants.action.bg}; color: ${color.light}; border: 1px solid ${buttonVariants.action.border};`;
	}

	return `color: ${color.muted}; border: 1px solid transparent;`;
}

/** Tab hover — only changes color when inactive. */
export function tabHover(isActive: boolean): (event: MouseEvent) => void {
	return (event: MouseEvent) => {
		if (!isActive) {
			(event.currentTarget as HTMLElement).style.color = color.body;
		}
	};
}

/** Tab mouseout — restore muted when inactive. */
export function tabOut(isActive: boolean): (event: MouseEvent) => void {
	return (event: MouseEvent) => {
		if (!isActive) {
			(event.currentTarget as HTMLElement).style.color = color.muted;
		}
	};
}

// ── Inventory Slot Helpers ──────────────────────────────────────

/** Style for an inventory slot (filled vs empty). */
export function slotStyle(hasItem: boolean): string {
	if (hasItem) {
		return `border-color: ${color.divider}; background: ${buttonVariants.secondary.bg}; cursor: pointer;`;
	}

	return `border-color: ${color.emptySlotBorder}; background: ${color.emptySlotBg};`;
}

/** Slot hover (only when filled). */
export function slotHover(hasItem: boolean): (event: MouseEvent) => void {
	return (event: MouseEvent) => {
		if (hasItem) {
			(event.currentTarget as HTMLElement).style.background = buttonVariants.secondary.hoverBg;
		}
	};
}

/** Slot mouseout (only when filled). */
export function slotOut(hasItem: boolean): (event: MouseEvent) => void {
	return (event: MouseEvent) => {
		if (hasItem) {
			(event.currentTarget as HTMLElement).style.background = buttonVariants.secondary.bg;
		}
	};
}

// ── HP / Resource Bar ───────────────────────────────────────────

/** Track (background) style for an HP-like bar. */
export const barTrackStyle = `background: ${color.barTrack}; border-color: ${color.barBorder};`;

/** Fill style for an HP bar at a given percentage. */
export function barFillStyle(pct: number, fill = color.hpFill): string {
	return `background: ${fill}; width:${pct}%`;
}
