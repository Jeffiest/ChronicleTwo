# Upstream sources stay in the tree for ownership and auditability. These units
# duplicate retained retail code or complete native replacements and must not
# enter either PORT_SOURCES or the PORT_OVERRIDE definition collector.
set(CT_NATIVE_SUPERSEDED_SOURCES
    "${PORT_DIR}/dng_hud.cpp"           # abort stub; keep retained retail implementation
    "${PORT_DIR}/funcpoint.cpp"         # abort stub; keep retained retail implementation
    "${PORT_DIR}/gyorace.cpp"           # abort stub; keep retained retail implementation
    "${PORT_DIR}/menucapt.cpp"          # abort stub; keep retained retail implementation
    "${PORT_DIR}/menucls1.cpp"          # abort stub; keep retained retail implementation
    "${PORT_DIR}/sceneevent.cpp"        # abort stub; keep retained retail implementation
    "${PORT_DIR}/collision_asm.cpp"       # collision transforms: kernel.cpp; pickup remains retail
    "${PORT_DIR}/editcoll.cpp"            # ClipBoxXZ remains in retail editcoll.cpp
    "${PORT_DIR}/editmap2.cpp"            # PlaneNormalXZ remains private to retail editmap2.cpp
    "${PORT_DIR}/gameutil_asm.cpp"        # gameutil_motion.cpp and vu0_gaps.cpp
    "${PORT_DIR}/linknames.cpp"           # runtime.cpp, eekernel.cpp, title.cpp, and sce/vuprog.cpp
    "${PORT_DIR}/mg_frame_asm.cpp"        # mg_frame_math.cpp owns matrices; culling helpers live there too
    "${PORT_DIR}/platform/settings.cpp"   # platform/config.cpp
)

# The native port supplies these SDK families through eekernel.cpp, movie.cpp,
# mglib.cpp and sce/*.cpp. libvu0 and the shared unimplemented reporter stay shared.
set(CT_NATIVE_SDK_FAMILIES
    libpad libmc sifdev sifrpc eekernel libcdvd libgraph libdma libpkt
    libdev libsdr modmsin sifdma)
