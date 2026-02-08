import {type FlatXoConfig} from 'xo';
import sveltePlugin from 'eslint-plugin-svelte';
import svelteParser from 'svelte-eslint-parser';
import tsParser from '@typescript-eslint/parser';

const config: FlatXoConfig = [
	{
		space: 2,
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
				project: ['./tsconfig.app.json', './tsconfig.node.json'],
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
			'@typescript-eslint/naming-convention': 'off',
			'@stylistic/max-len': 'off',
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
