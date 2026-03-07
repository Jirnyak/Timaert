// === Procedural Monster Generator ===
// Generates organic creature sprites using Canvas API

export class MonsterGenerator {
	private seed: number;
	private readonly ctx: CanvasRenderingContext2D;
	private readonly width = 128;
	private readonly height = 128;

	constructor(seed: number) {
		this.seed = seed;
		const canvas = document.createElement('canvas');
		canvas.width = this.width;
		canvas.height = this.height;
		this.ctx = canvas.getContext('2d', {willReadFrequently: true})!;
	}

	// === Helpers ===

	private random(): number {
		const x = Math.sin(this.seed++) * 10_000;
		return x - Math.floor(x);
	}

	private randRange(min: number, max: number): number {
		return this.random() * (max - min) + min;
	}

	private randInt(min: number, max: number): number {
		return Math.floor(this.randRange(min, max + 1));
	}

	private hsvToRgb(h: number, s: number, v: number): string {
		let r = 0; let g = 0; let
			b = 0;
		const i = Math.floor(h * 6);
		const f = h * 6 - i;
		const p = v * (1 - s);
		const q = v * (1 - f * s);
		const t = v * (1 - (1 - f) * s);

		switch (i % 6) {
			case 0: {r = v; g = t; b = p; break;
			}

			case 1: {r = q; g = v; b = p; break;
			}

			case 2: {r = p; g = v; b = t; break;
			}

			case 3: {r = p; g = q; b = v; break;
			}

			case 4: {r = t; g = p; b = v; break;
			}

			case 5: {r = v; g = p; b = q; break;
			}
		}

		return `rgb(${Math.floor(r * 255)}, ${Math.floor(g * 255)}, ${Math.floor(b * 255)})`;
	}

	private generatePalette() {
		const hue = this.random();
		const saturation = this.randRange(0.5, 0.9);
		const baseValue = this.randRange(0.4, 0.7);

		const base = this.hsvToRgb(hue, saturation, baseValue);
		const light = this.hsvToRgb(hue, saturation * 0.6, Math.min(baseValue + 0.2, 0.9));
		const dark = this.hsvToRgb(hue, saturation, baseValue * 0.6);

		const accentHue = (hue + 0.5) % 1;
		const accent = this.hsvToRgb(accentHue, saturation * 0.8, 0.8);

		return {
			base, light, dark, accent,
		};
	}

	// === Drawing Primitives ===

	private drawOrganicBlob(cx: number, cy: number, radius: number, color: string, points = 8) {
		const vertices: Array<{x: number; y: number}> = [];
		for (let i = 0; i < points; i++) {
			const angle = (i / points) * Math.PI * 2;
			const r = radius * this.randRange(0.7, 1.3);
			vertices.push({
				x: cx + r * Math.cos(angle),
				y: cy + r * Math.sin(angle),
			});
		}

		this.ctx.fillStyle = color;
		this.ctx.beginPath();
		this.ctx.moveTo(vertices[0].x, vertices[0].y);
		// Quadratic curve smoothing
		for (let i = 0; i < vertices.length; i++) {
			const p0 = vertices[i];
			const p1 = vertices[(i + 1) % vertices.length];
			const midX = (p0.x + p1.x) / 2;
			const midY = (p0.y + p1.y) / 2;
			this.ctx.quadraticCurveTo(p0.x, p0.y, midX, midY);
		}

		this.ctx.fill();
		return vertices; // Return for potential usage
	}

	private drawLimb(startX: number, startY: number, angle: number, length: number, thickness: number, color: string) {
		const segments = this.randInt(2, 3);
		let curX = startX;
		let curY = startY;
		let curAngle = angle;

		this.ctx.strokeStyle = color;
		this.ctx.fillStyle = color;
		this.ctx.lineCap = 'round';

		for (let i = 0; i < segments; i++) {
			const segLength = length / segments;
			const endX = curX + segLength * Math.cos(curAngle);
			const endY = curY + segLength * Math.sin(curAngle);

			const segThick = thickness * (1 - i * 0.3 / segments);

			this.ctx.lineWidth = segThick;
			this.ctx.beginPath();
			this.ctx.moveTo(curX, curY);
			this.ctx.lineTo(endX, endY);
			this.ctx.stroke();

			// Joint
			this.ctx.beginPath();
			this.ctx.arc(endX, endY, segThick / 2, 0, Math.PI * 2);
			this.ctx.fill();

			curX = endX;
			curY = endY;
			curAngle += this.randRange(-0.3, 0.3);
		}
	}

	private drawSymmetrical(cx: number, cy: number, offX: number, offY: number, size: number, color: string) {
		this.ctx.fillStyle = color;
		// Left
		this.ctx.beginPath();
		this.ctx.ellipse(cx - offX, cy + offY, size / 2, size / 2, 0, 0, Math.PI * 2);
		this.ctx.fill();
		// Right
		this.ctx.beginPath();
		this.ctx.ellipse(cx + offX, cy + offY, size / 2, size / 2, 0, 0, Math.PI * 2);
		this.ctx.fill();
	}

	// === Main Generation ===

	public generate(): HTMLCanvasElement {
		const {ctx, width, height} = this;
		const cx = width / 2;
		const cy = height / 2;
		const palette = this.generatePalette();

		ctx.clearRect(0, 0, width, height);

		// 1. Body
		const bodyType = this.randInt(0, 2); // 0=blob, 1=seg, 2=tall
		let headY = cy;
		const bodyRegions: Array<{x: number; y: number; r: number}> = [];

		if (bodyType === 0) { // Blob
			const r = this.randInt(24, 34);
			const y = cy + this.randInt(-5, 6);
			this.drawOrganicBlob(cx, y, r, palette.base, 10);
			bodyRegions.push({x: cx, y, r});
			headY = y - r / 2;
		} else if (bodyType === 1) { // Segmented
			const segs = this.randInt(2, 4);
			const r = this.randInt(18, 24);
			const spacing = this.randInt(14, 18);
			const startY = cy - (segs - 1) * spacing / 2;

			// Draw connections first
			ctx.strokeStyle = palette.dark;
			ctx.lineWidth = r;
			ctx.beginPath();
			ctx.moveTo(cx, startY);
			ctx.lineTo(cx, startY + (segs - 1) * spacing);
			ctx.stroke();

			for (let i = 0; i < segs; i++) {
				const y = startY + i * spacing;
				this.drawOrganicBlob(cx, y, r, palette.base, 8);
				bodyRegions.push({x: cx, y, r});
			}

			headY = startY - r;
		} else { // Tall
			const h = this.randInt(50, 70);
			const w = this.randInt(22, 32);
			this.ctx.fillStyle = palette.base;
			this.ctx.beginPath();
			this.ctx.ellipse(cx, cy, w / 2, h / 2, 0, 0, Math.PI * 2);
			this.ctx.fill();
			bodyRegions.push({x: cx, y: cy, r: w / 2});
			headY = cy - h / 2 + 10;
		}

		// 2. Head
		const headSize = this.randInt(18, 26);
		this.drawOrganicBlob(cx, headY, headSize, palette.base, 8);

		// 3. Limbs (Draw behind body? No, monster.py draws them on top mostly, or order varies. Let's draw on top for now)
		const limbPairs = this.randInt(1, 3);
		const limbYStart = cy;
		for (let i = 0; i < limbPairs; i++) {
			const y = limbYStart + (i - limbPairs / 2) * 15;
			const length = this.randInt(20, 35);
			const thick = this.randInt(4, 8);
			const angle = this.randRange(0.3, 1.5); // Left side, down-ish

			this.drawLimb(cx - 10, y, Math.PI - angle + 0.3, length, thick, palette.dark); // Left
			this.drawLimb(cx + 10, y, angle - 0.3, length, thick, palette.dark); // Right
		}

		// 4. Eyes
		const eyeOffX = this.randInt(headSize / 3, headSize / 2);
		const eyeOffY = this.randInt(-5, 5);
		const eyeSize = this.randInt(6, 10);
		this.drawSymmetrical(cx, headY, eyeOffX, eyeOffY, eyeSize, 'white');
		this.drawSymmetrical(cx, headY, eyeOffX, eyeOffY, eyeSize / 2, 'black');

		// 5. Textures (Source Atop)
		ctx.globalCompositeOperation = 'source-atop';

		// Random spots/noise on body
		const texType = this.randInt(0, 2);
		ctx.fillStyle = palette.dark;
		if (texType === 0) { // Spots
			for (let i = 0; i < 50; i++) {
				const x = this.randInt(0, width);
				const y = this.randInt(0, height);
				const s = this.randInt(2, 6);
				ctx.beginPath();
				ctx.arc(x, y, s, 0, Math.PI * 2);
				ctx.fill();
			}
		} else if (texType === 1) { // Stripes
			ctx.lineWidth = 2;
			ctx.strokeStyle = palette.dark;
			for (let i = 0; i < 20; i++) {
				const y = this.randInt(0, height);
				ctx.beginPath();
				ctx.moveTo(0, y);
				ctx.lineTo(width, y + this.randInt(-10, 10));
				ctx.stroke();
			}
		}

		// Highlight Edges (Fake Rim Light)
		// We can cheat by drawing a large transparent radial gradient or inner shadow
		// Or pixel manipulation like in python. Let's do pixel manip for "Noise"

		ctx.globalCompositeOperation = 'source-over'; // Reset

		// 6. Post Processing (Noise)
		const imgData = ctx.getImageData(0, 0, width, height);
		const {data} = imgData;
		for (let i = 0; i < data.length; i += 4) {
			if (data[i + 3] > 0) { // If visible
				const noise = (this.random() - 0.5) * 30;
				data[i] = Math.max(0, Math.min(255, data[i] + noise));
				data[i + 1] = Math.max(0, Math.min(255, data[i + 1] + noise));
				data[i + 2] = Math.max(0, Math.min(255, data[i + 2] + noise));
			}
		}

		ctx.putImageData(imgData, 0, 0);

		return this.ctx.canvas;
	}
}
