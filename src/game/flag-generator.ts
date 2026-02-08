// === Procedural Flag/Banner Generator ===
// Faithful port of flag_generator.py using Canvas API

export class FlagGenerator {
	private ctx: CanvasRenderingContext2D;
	private size = 128;
	private seed: number;

	constructor(seed: number) {
		this.seed = seed;
		const canvas = document.createElement('canvas');
		canvas.width = this.size;
		canvas.height = this.size;
		this.ctx = canvas.getContext('2d')!;
	}

	private random(): number {
		const x = Math.sin(this.seed++) * 10000;
		return x - Math.floor(x);
	}

	private randInt(min: number, max: number): number {
		return Math.floor(this.random() * (max - min + 1)) + min;
	}

	private pickColor(minV = 50, maxV = 220): string {
		const r = this.randInt(minV, maxV);
		const g = this.randInt(minV, maxV);
		const b = this.randInt(minV, maxV);
		return `rgb(${r}, ${g}, ${b})`;
	}

	private pickPalette() {
		return [
			this.pickColor(50, 200),  // Base
			this.pickColor(80, 240),  // Accent
			this.pickColor(60, 220),  // Detail
			this.pickColor(120, 255)  // Highlight
		];
	}

	public generate(): HTMLCanvasElement {
		const palette = this.pickPalette();
		const {ctx, size} = this;

		// 1. Background
		ctx.fillStyle = palette[0];
		ctx.fillRect(0, 0, size, size);

		if (this.random() > 0.5) {
			ctx.fillStyle = palette[1];
			for (let i = 0; i < size; i += 16) {
				if (this.random() > 0.4) ctx.fillRect(0, i, size, 8);
			}
		}

		// 2. Main Body
		ctx.fillStyle = palette[1];
		const modes = ['rect', 'diagonal', 'circle', 'split'];
		const mode = modes[this.randInt(0, modes.length - 1)];

		if (mode === 'rect') {
			const m = this.randInt(12, 24);
			ctx.fillRect(m, m, size - m * 2, size - m * 2);
		} else if (mode === 'diagonal') {
			const t = this.randInt(16, 32);
			ctx.beginPath();
			ctx.moveTo(0, t); ctx.lineTo(t, 0);
			ctx.lineTo(size, size - t); ctx.lineTo(size - t, size);
			ctx.fill();
		} else if (mode === 'circle') {
			const r = this.randInt(24, 40);
			ctx.beginPath();
			ctx.arc(size / 2, size / 2, r, 0, Math.PI * 2);
			ctx.fill();
		} else {
			const split = this.randInt(48, 80);
			ctx.fillRect(0, 0, split, size);
		}

		// 3. Divisors & Symbols
		ctx.fillStyle = palette[2];
		const symModes = ['cross', 'diamond', 'triangles', 'bars'];
		const symMode = symModes[this.randInt(0, symModes.length - 1)];

		if (symMode === 'cross') {
			const t = 10;
			ctx.fillRect(size / 2 - t / 2, size / 4, t, size / 2);
			ctx.fillRect(size / 4, size / 2 - t / 2, size / 2, t);
		} else if (symMode === 'diamond') {
			const s = 56 / 2;
			ctx.beginPath();
			ctx.moveTo(size / 2, size / 2 - s);
			ctx.lineTo(size / 2 + s, size / 2);
			ctx.lineTo(size / 2, size / 2 + s);
			ctx.lineTo(size / 2 - s, size / 2);
			ctx.fill();
		} else if (symMode === 'triangles') {
			ctx.beginPath();
			ctx.moveTo(0, 0); ctx.lineTo(size / 2, size / 2); ctx.lineTo(0, size); ctx.fill();
			ctx.beginPath();
			ctx.moveTo(size, 0); ctx.lineTo(size / 2, size / 2); ctx.lineTo(size, size); ctx.fill();
		} else {
			for (let i = 0; i < 3; i++) ctx.fillRect(8, 24 + i * 24, size - 16, 8);
		}

		// 4. Additional Details
		ctx.fillStyle = palette[3];
		const detModes = ['stars', 'circles', 'squares'];
		const detMode = detModes[this.randInt(0, detModes.length - 1)];

		if (detMode === 'stars') {
			for (let i = 0; i < this.randInt(3, 6); i++) {
				const x = this.randInt(16, size - 16);
				const y = this.randInt(16, size - 16);
				ctx.fillRect(x - 4, y - 1, 8, 2);
				ctx.fillRect(x - 1, y - 4, 2, 8);
			}
		} else if (detMode === 'circles') {
			for (let i = 0; i < this.randInt(4, 8); i++) {
				ctx.beginPath();
				ctx.arc(this.randInt(12, size - 12), this.randInt(12, size - 12), this.randInt(3, 6), 0, Math.PI * 2);
				ctx.fill();
			}
		} else {
			for (let i = 0; i < this.randInt(4, 8); i++) {
				const s = this.randInt(6, 12);
				ctx.fillRect(this.randInt(12, size - 12), this.randInt(12, size - 12), s, s);
			}
		}

		return ctx.canvas;
	}
}
