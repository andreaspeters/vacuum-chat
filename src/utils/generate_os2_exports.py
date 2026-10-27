#!/usr/bin/env python3
"""Generate an OS/2 DLL module definition from the target's actual link inputs."""

from __future__ import annotations

import argparse
import pathlib
import shlex
import subprocess
import sys


def collect_object_inputs(
    link_command_file: pathlib.Path, working_directory: pathlib.Path
) -> list[pathlib.Path]:
    """Resolve object-file arguments, including CMake response files."""
    arguments = shlex.split(link_command_file.read_text(encoding="utf-8"))
    response_stack: set[pathlib.Path] = set()

    def expand_response_files(items: list[str]):
        for item in items:
            if not item.startswith("@") or item == "@":
                yield item
                continue

            response_file = pathlib.Path(item[1:])
            if not response_file.is_absolute():
                response_file = working_directory / response_file
            response_file = response_file.resolve()
            if response_file in response_stack:
                raise ValueError(f"recursive response file: {response_file}")

            response_stack.add(response_file)
            try:
                response_arguments = shlex.split(
                    response_file.read_text(encoding="utf-8")
                )
                yield from expand_response_files(response_arguments)
            finally:
                response_stack.remove(response_file)

    objects: list[pathlib.Path] = []
    for argument in expand_response_files(arguments):
        if pathlib.Path(argument).suffix.lower() not in {".o", ".obj"}:
            continue
        path = pathlib.Path(argument)
        if not path.is_absolute():
            path = working_directory / path
        path = path.resolve()
        if not path.is_file():
            raise FileNotFoundError(f"target link input is missing: {path}")
        if path not in objects:
            objects.append(path)
    return objects


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
        objects = collect_object_inputs(link_command_file, working_directory)
    except (OSError, ValueError) as error:
        print(f"cannot read target link inputs: {error}", file=sys.stderr)
        return 1

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
