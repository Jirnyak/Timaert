# AGENTS

## Performance-first code

All new code should prioritize high performance. Favor better algorithms, data layouts, and containers to maximize FPS and avoid UI freezes.

## No exceptions / no RTTI

Do not rely on C++ exceptions or RTTI in this codebase. They are disabled for both performance and binary size reasons.
