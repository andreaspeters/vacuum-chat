# Vacuum IM

Vacuum IM is a Qt 6 desktop instant-messaging client with a modular plugin architecture. The project includes an established XMPP client and a Matrix Client-Server plugin, with shared interfaces for conversations, presence, rosters, notifications, and message display.

## Features

- XMPP accounts, presence and contact rosters
- One-to-one chats and multi-user chat rooms
- Message history and archive support
- File transfer, notifications, and configurable message styles
- Extensible functionality provided by loadable plugins
- Matrix rooms and conversations integrated into the shared chat UI
- Optional Matrix end-to-end encryption support when `libolm` is available at build time

## Requirements

- CMake 3.16 or newer
- A C++ compiler supported by Qt 6
- Qt 6 components: Core, Gui, Widgets, Network, Xml, and Sql
- Python 3 for the protocol smoke tests

Matrix encryption dependencies are optional. CMake enables Matrix E2EE when it finds `libolm`; OpenSSL enables the Matrix Secure Secret Storage (SSSS) functionality. Without these libraries, the corresponding Matrix cryptographic features are disabled.

## Build

Configure and build the main application from the repository root:

```sh
cmake -S . -B build
cmake --build build --target vacuumu -j1
```

The repository also provides Makefile shortcuts:

```sh
make build
```

To build the Matrix plugin explicitly:

```sh
cmake --build build --target matrix -j1
```

Use a separate build directory by setting `BUILD_DIR`, for example:

```sh
BUILD_DIR=build-nix make build
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

Vacuum IM is distributed under the GNU General Public License, version 3. See [`COPYING`](COPYING) for the full license text.
