// === Minimal 3D math for subworld renderer ===
//
// Column-major mat4 operations + vec3 helpers.
// No dependency on external libraries — Quake-style minimal.

export type Mat4 = Float32Array;
export type Vec3 = [number, number, number];

export function mat4Create(): Mat4 {
	const m = new Float32Array(16);
	m[0] = 1;
	m[5] = 1;
	m[10] = 1;
	m[15] = 1;
	return m;
}

export function mat4Perspective(fov: number, aspect: number, near: number, far: number): Mat4 {
	const m = new Float32Array(16);
	const f = 1 / Math.tan(fov / 2);
	const rangeInv = 1 / (near - far);
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (near + far) * rangeInv;
	m[11] = -1;
	m[14] = 2 * near * far * rangeInv;
	return m;
}

export function mat4LookAt(eye: Vec3, target: Vec3, up: Vec3): Mat4 {
	let zx = eye[0] - target[0];
	let zy = eye[1] - target[1];
	let zz = eye[2] - target[2];
	let len = Math.sqrt(zx * zx + zy * zy + zz * zz);
	if (len > 0) {
		zx /= len;
		zy /= len;
		zz /= len;
	}

	// Cross(up, z) → x-axis
	let xx = up[1] * zz - up[2] * zy;
	let xy = up[2] * zx - up[0] * zz;
	let xz = up[0] * zy - up[1] * zx;
	len = Math.sqrt(xx * xx + xy * xy + xz * xz);
	if (len > 0) {
		xx /= len;
		xy /= len;
		xz /= len;
	}

	// Cross(z, x) → y-axis
	const yx = zy * xz - zz * xy;
	const yy = zz * xx - zx * xz;
	const yz = zx * xy - zy * xx;

	const m = new Float32Array(16);
	m[0] = xx;
	m[1] = yx;
	m[2] = zx;
	m[4] = xy;
	m[5] = yy;
	m[6] = zy;
	m[8] = xz;
	m[9] = yz;
	m[10] = zz;
	m[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
	m[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
	m[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
	m[15] = 1;
	return m;
}

export function mat4Multiply(a: Mat4, b: Mat4): Mat4 {
	const out = new Float32Array(16);
	for (let i = 0; i < 4; i++) {
		for (let j = 0; j < 4; j++) {
			out[j * 4 + i]
				= a[i] * b[j * 4]
				+ a[4 + i] * b[j * 4 + 1]
				+ a[8 + i] * b[j * 4 + 2]
				+ a[12 + i] * b[j * 4 + 3];
		}
	}

	return out;
}

export function mat4Translate(m: Mat4, x: number, y: number, z: number): Mat4 {
	const out = new Float32Array(m);
	out[12] += m[0] * x + m[4] * y + m[8] * z;
	out[13] += m[1] * x + m[5] * y + m[9] * z;
	out[14] += m[2] * x + m[6] * y + m[10] * z;
	return out;
}

export function mat4Scale(m: Mat4, sx: number, sy: number, sz: number): Mat4 {
	const out = new Float32Array(m);
	out[0] *= sx;
	out[1] *= sx;
	out[2] *= sx;
	out[4] *= sy;
	out[5] *= sy;
	out[6] *= sy;
	out[8] *= sz;
	out[9] *= sz;
	out[10] *= sz;
	return out;
}

export function mat4RotateY(m: Mat4, angle: number): Mat4 {
	const c = Math.cos(angle);
	const s = Math.sin(angle);
	const out = new Float32Array(m);
	const m0 = m[0];
	const m1 = m[1];
	const m2 = m[2];
	const m8 = m[8];
	const m9 = m[9];
	const m10 = m[10];
	out[0] = m0 * c + m8 * s;
	out[1] = m1 * c + m9 * s;
	out[2] = m2 * c + m10 * s;
	out[8] = m8 * c - m0 * s;
	out[9] = m9 * c - m1 * s;
	out[10] = m10 * c - m2 * s;
	return out;
}

export function vec3Normalize(v: Vec3): Vec3 {
	const len = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len === 0) {
		return [0, 0, 0];
	}

	return [v[0] / len, v[1] / len, v[2] / len];
}

export function vec3Sub(a: Vec3, b: Vec3): Vec3 {
	return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

export function vec3Cross(a: Vec3, b: Vec3): Vec3 {
	return [
		a[1] * b[2] - a[2] * b[1],
		a[2] * b[0] - a[0] * b[2],
		a[0] * b[1] - a[1] * b[0],
	];
}
