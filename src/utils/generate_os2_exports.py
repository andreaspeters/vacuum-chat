#!/usr/bin/env python3
"""Generate an OS/2 DLL module definition from the target's actual link inputs."""

from __future__ import annotations

import argparse
import pathlib
import shlex
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emxexp", required=True)
    parser.add_argument("--link-command-file", required=True)
    parser.add_argument("--working-directory", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    working_directory = pathlib.Path(args.working_directory).resolve()
    link_command_file = pathlib.Path(args.link_command_file)
    try:
        link_arguments = shlex.split(link_command_file.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"cannot read target link command {link_command_file}: {error}", file=sys.stderr)
        return 1

    objects: list[pathlib.Path] = []
    for argument in link_arguments:
        if argument.lower().endswith(".obj"):
            path = pathlib.Path(argument)
            if not path.is_absolute():
                path = working_directory / path
            path = path.resolve()
            if not path.is_file():
                print(f"target link input is missing: {path}", file=sys.stderr)
                return 1
            if path not in objects:
                objects.append(path)
    if not objects:
        print(f"no OMF object inputs found in {link_command_file}", file=sys.stderr)
        return 1

    result = subprocess.run(
        [args.emxexp, "-u", *(str(obj) for obj in objects)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        sys.stderr.write(result.stderr)
        return result.returncode

    export_text = result.stdout.rstrip()
    required_symbols = (
        "__ZN7Options8instanceEv",
        "__ZN7Options1dE",
        "__ZTV19AnimatedTextBrowser",
    )
    for symbol in required_symbols:
        if symbol not in export_text:
            print(f"generated exports omit required symbol {symbol}", file=sys.stderr)
            return 1

    output = pathlib.Path(args.output)
    output.write_text(
        "LIBRARY vacuum INITINSTANCE TERMINSTANCE\n"
        "DESCRIPTION 'Vacuum shared utility library'\n"
        "EXPORTS\n"
        + export_text
        + "\n",
        encoding="utf-8",
    )
    print(f"Generated OS/2 exports from {len(objects)} target object files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
