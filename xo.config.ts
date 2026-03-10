import {type FlatXoConfig} from 'xo';
import sveltePlugin from 'eslint-plugin-svelte';
import svelteParser from 'svelte-eslint-parser';
import tsParser from '@typescript-eslint/parser';

const config: FlatXoConfig = [
	{
		files: ['**/*.svelte'],
		plugins: {
			// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
			svelte: sveltePlugin,
		},
		languageOptions: {
			// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
			parser: svelteParser,
			parserOptions: {
				// This allows the Svelte parser to handle TS inside <script> tags
				// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
				parser: tsParser,
				// Tell the parser to look for your tsconfig
				project: ['./tsconfig.json'],
				extraFileExtensions: ['.svelte'],
			},
		},
		// Apply the recommended Svelte rules
		// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
		rules: {
			// eslint-disable-next-line @typescript-eslint/no-unsafe-call
			...sveltePlugin.configs['flat/recommended']
				// eslint-disable-next-line @typescript-eslint/no-unsafe-return
				.map(c => c.rules)
				// eslint-disable-next-line @typescript-eslint/no-unsafe-return, unicorn/no-array-reduce
				.reduce((acc, rules) => ({...acc, ...rules}), {}),
			// ESLint can't see Svelte template usage — these are false positives
			'no-unused-vars': 'off',
			'no-undef': 'off',
			// Svelte each blocks don't always need keys (static lists, index-based)
			'svelte/require-each-key': 'off',
			// `e` is idiomatic for event handlers in Svelte templates
			'unicorn/prevent-abbreviations': 'off',
			// Svelte bind: patterns use return-assign in event arrows
			'no-return-assign': 'off',
			// False positives from template-level ignores
			'svelte/no-unused-svelte-ignore': 'off',
			// SvelteMap/SvelteSet not required for non-reactive contexts
			'svelte/prefer-svelte-reactivity': 'off',
			// Perf: Math.hypot is ~3x slower
			'unicorn/prefer-modern-math-apis': 'off',
		},
	},
	{
		files: ['vite.config.*', 'svelte.config.*'],
		rules: {
			'@typescript-eslint/no-unsafe-assignment': 'off',
			'@typescript-eslint/no-unsafe-call': 'off',
		},
	},
	{
		rules: {
			'import-x/extensions': 'off',
			'import-x/no-extraneous-dependencies': 'off',
			'n/no-extraneous-import': 'off',
			'@typescript-eslint/naming-convention': 'off',
			'@stylistic/max-len': 'off',
			'max-lines': ['warn', {max: 2000, skipBlankLines: true, skipComments: true}],
			// Perf: Math.hypot is ~3x slower than Math.sqrt
			'unicorn/prefer-modern-math-apis': 'off',
			'no-bitwise': 'off',
			'@stylistic/no-mixed-operators': 'off',
			complexity: 'off',
			'max-params': 'off',
			'@typescript-eslint/member-ordering': 'off',
			'unicorn/filename-case': [
				'error',
				{
					cases: {
						kebabCase: true,
						pascalCase: true,
					},
				},
			],
		},
	},
];

export default config;
