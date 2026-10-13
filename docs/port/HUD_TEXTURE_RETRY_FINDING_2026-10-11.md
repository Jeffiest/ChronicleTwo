# HUD texture retry finding (2026-10-11)

`mgCTextureManager` keeps host texture bindings in `g_entries`. `Sync` clears an entry's dirty bit before trying renderer allocation. If `gfx::CreateTexture` returns the null handle (including temporary texture-slot exhaustion while recorded frames retain resources), the entry previously remained clean with a null texture. Later `PortMgTextureBinding` calls therefore skipped `Sync` permanently and reported no binding for that game texture.

The port now restores the dirty bit when `CreateTexture` fails. The next bind can retry after renderer resources become available. This is a narrow recovery fix; it does not establish that the reported `0x10005` stderr handle was caused by this path.

Evidence: `Sync`'s allocation-failure branch and `PortMgTextureBinding`'s `if (entry.dirty) Sync(...)` gate in `port/src/mg_texture.cpp`; gfx texture allocation reports and returns `kNullTexture` when its slot allocator is exhausted in `port/src/host_overrides/gfx/resources.cpp`.

Validation: source inspection only. No focused runtime fixture was wired because this batch excludes build-system changes; no runtime reproduction of transient slot exhaustion was available.
