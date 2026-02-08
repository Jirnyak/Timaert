import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {svelte} from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';
import {defineConfig} from 'vite';
import {viteSingleFile} from 'vite-plugin-singlefile';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// https://vite.dev/config/
export default defineConfig({
	plugins: [svelte(), tailwindcss(), viteSingleFile()],
	resolve: {
		alias: {
			'@': path.resolve(__dirname, 'src'),
		},
	},
});
