# OS/2 GCC 9 does not ship a separate libatomic archive, while the Qt6
# package configuration otherwise requires one after its 64-bit atomic probe.
# Keep the imported interface target library-free; unresolved atomic symbols,
# if any, are then reported by the actual target link.
if(NOT TARGET WrapAtomic::WrapAtomic)
    add_library(WrapAtomic::WrapAtomic INTERFACE IMPORTED)
endif()
set(WrapAtomic_FOUND TRUE)
