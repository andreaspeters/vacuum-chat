# Matrix crypto lifecycle regression

This standalone test compiles the actual `MatrixOlmCrypto` and `MatrixDatabase`
implementations and uses real libolm with synthetic accounts and temporary SQLite
profiles. It does not read existing Vacuum profiles or contact a homeserver.

Coverage:

- cached and unpickled outbound Megolm identity/key export;
- rejection of mismatched outbound pickle/session IDs;
- cached, restored and forwarded inbound Megolm decryption;
- outbound ratchet indexes after successive messages;
- Olm PRE_KEY and bidirectional type-1 messages;
- active-cache clearing, persisted candidate selection and account/session restore.

```sh
cmake -S tests/crypto -B build-crypto-tests -DOLM_ROOT=/path/to/libolm/prefix
cmake --build build-crypto-tests -j1
ctest --test-dir build-crypto-tests --output-on-failure
```

Prerequisites: C++17 compiler, Qt6 Core/Sql development files, the Qt SQLite
plugin, and libolm development files. `OLM_ROOT` is optional when libolm is on the
normal compiler/library search path. CTest bounds the executable to 15 seconds.

For a mixed Nix compiler/system-Qt environment, unset injected Nix compile/link
flags when configuring and building. If the Nix linker cannot locate indirect
system libraries, configure with `-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link,/usr/lib`.
Run CTest with the actual Qt/libolm runtime directories in `LD_LIBRARY_PATH`
when those libraries are not in the loader's default search path.

A pass proves local crypto/persistence behavior, not homeserver transport,
Element processing, device trust or successful live two-device E2EE.
