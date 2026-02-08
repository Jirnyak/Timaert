# Samosbor

Web-first rewrite of **Samosbor**, a procedural settlement and world simulation currently built with Svelte 5, Tailwind CSS 4, and a custom WebGL2 renderer. Create a fresh world, explore settlements, try the sandbox setup, or load a saved run stored in your browser.

## Features

- Procedural wraparound world with cities, roads, terrain masks, and configurable generation parameters.
- Local saves in `localStorage` (autosave + manual load screen).
- Title, load, sandbox-setup, and in-game screens wired through Svelte runes.
- Background music with in-app mute toggle.
- Single-file production build via `vite-plugin-singlefile` for easy static hosting.

## Quick start

Prerequisites: Node.js 18+ and npm.

```bash
npm install
npm run dev   # start dev server (defaults to http://localhost:5173)
```

Open the shown URL, start a **New Game** or **Sandbox** from the title screen, and your saves will be kept locally.

## Build

```bash
npm run build           # outputs a single-file bundle into dist/
npm run preview         # serve the production build locally
```

Deploy by serving `dist/index.html` (and the bundled assets it inlines) from any static host.

## Scripts

- `npm run dev` — Vite dev server.
- `npm run build` — production build (single-file output).
- `npm run preview` — preview the production build locally.
- `npm test` — run XO lint checks.

## Project structure

- `src/` — Svelte components, game logic, and WebGL renderer.
  - `game/` — simulation state, items, attributes, pathfinding, audio helpers.
  - `screens/` — title/load/sandbox/game screens.
  - `webgl/` — world generation parameters and renderer utilities.
- `public/` — static assets (sprites, audio) copied as-is.
- `old_cxx_version/` — legacy C++ implementation. See its README for CMake/Ninja build steps.

## Development notes

- Uses Svelte 5 runes and Tailwind CSS 4 (via `@tailwindcss/vite`).
- Vite alias `@` points to `src/`.
- Lint with `npm test` (XO). Align with the rules in `AGENTS.md`.
