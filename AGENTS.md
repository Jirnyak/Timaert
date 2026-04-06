---
# Agent Instructions

## Lint & Style (XO)
- Use **single quotes** for strings in Svelte `<script>` and template expressions.
- For single-argument arrow functions, omit parentheses: `value => fn(value)`.
- Keep operators and `=` at line starts when breaking lines (XO operator-linebreak rules).
- Avoid newline after opening `(` or before closing `)` when calling functions.
- Run `npx xo` before committing; use `npx xo --fix` first, then clean remaining items manually.

## UI & Layout
- Prefer Tailwind **`font-sans`** as the default. Do **not** force `font-mono` unless showing code.
- Let tab labels size naturally; avoid fixed-width tabs that crop text. If space is tight, wrap or use fade/scroll-without-scrollbar techniques instead of visible scrollbars.
- When changing typography, keep spacing consistent—check for overflow/scrollbars introduced by font changes.

## Workflow
1) After UI changes, visually inspect for clipping/scroll artifacts in tab bars and panels.
2) Run `npx xo` and fix reported issues; align with XO stylistic rules above.
3) Keep CSS/Tailwind utility usage consistent with existing patterns (e.g., `font-sans`, `text-sm`).
4) When adjusting global styles, verify component-level overrides don’t reintroduce monospace fonts.

## File Organization
- One file = one responsibility
- Do NOT split files to meet an arbitrary line count
- A 500-line parser that does one thing well is better than five
  100-line files importing from each other
- DO split when there is a genuine architectural reason:
  • Pure logic vs. DOM-dependent code (for worker compatibility)
  • Pure rendering functions vs. Svelte component (for testability)
  • Shared utilities used by 3+ consumers
  • Types/interfaces into a dedicated types.ts
- If a single file exceeds ~800 lines, look for a natural seam —
  but only split if the two halves are genuinely independent
- Never let a file exceed 1000 lines — this indicates mixed
  responsibilities that should be untangled
  (**Max ~1000 lines** per file, relaxed for naturally encapsulated modules (renderers, generators).) If encapsulated dungeon generator.ts is > 1000 lines but has only this single purpose thats totally fine.

  ## Save Game Compatibility

No need to maintain save compatibility (forward or backward) as the game is in early development stage. For any breaking changes to save format, simply increment `kSaveVersion`. All existing saves will be invalidated automatically.

## Legacy Code Removal

All legacy code should be removed as soon as possible since the game is in early development. There's no need to maintain backward compatibility or keep deprecated code paths.

## Performance-first code

All new code should prioritize high performance. Favor better algorithms, data layouts, and containers to maximize FPS and avoid UI freezes.