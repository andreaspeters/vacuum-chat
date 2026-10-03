# Vacuum Chat

Vacuum Chat 2.0.0 is a Qt 6 desktop messaging application with a modular plugin architecture. It offers XMPP account support and an integrated Matrix plugin, with shared interfaces for conversations, presence, rosters, notifications, and message display.

## Features

- XMPP accounts, presence and contact rosters
- One-to-one chats and multi-user chat rooms
- Message history and archive support
- File transfer, notifications, and configurable message styles
- Extensible functionality provided by loadable plugins
- Matrix rooms and conversations integrated into the shared chat UI
- Optional Matrix end-to-end encryption support when `libolm` is available at build time
- MeshCore account

## Requirements

- CMake 3.16 or newer
- A C++ compiler supported by Qt 6
- Qt 6 components: Core, Gui, Widgets, Network, Xml, and Sql
- Python 3 for the protocol smoke tests

Matrix encryption dependencies are optional. CMake enables Matrix E2EE when it finds `libolm`; OpenSSL enables the Matrix Secure Secret Storage (SSSS) functionality. Without these libraries, the corresponding Matrix cryptographic features are disabled.

## Build

Configure and build the main application from the repository root:

```sh
mkdir build
cd build
cmake -DRUN_FROM_BUILD_DIR=ON -DHAVE_OLM=1 ..
make

```

## Packaging

On Linux, CPack can create a Debian package; when `bsdtar` and `zstd` are
available, its default generator set also creates an Arch Linux
`.pkg.tar.zst` package:

```sh
cmake -DRUN_FROM_BUILD_DIR=OFF -DHAVE_OLM=1 -DCPACK_BINARY_DEB=true
make packages
```

## Tests

Run the project checks with:

```sh
make check
```

This builds the `chatmessagehandler` and `recentcontacts` targets, runs the Matrix protocol smoke suite, and checks the Git diff for whitespace errors. The Matrix smoke suite uses a local mock HTTP server and does not require a live homeserver.

An optional live Matrix smoke test runs when all of these environment variables are set:

- `MATRIX_HOMESERVER`
- `MATRIX_ACCESS_TOKEN`
- `MATRIX_USER_ID`
- `MATRIX_HOME_ROOM`

Keep access tokens private; do not commit them or place them in this README.

## Project layout

- `src/interfaces/` — protocol-neutral plugin and application interfaces
- `src/plugins/` — XMPP, Matrix, chat UI, roster, notification, and other plugins
- `src/utils/` — shared application utilities
- `tests/` — protocol and integration smoke tests

## License

Vacuum Chat is distributed under the GNU General Public License, version 3. See [`COPYING`](COPYING) for the full license text.
