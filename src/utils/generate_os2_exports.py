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


def collect_ninja_object_inputs(
    ninja_executable: pathlib.Path,
    build_directory: pathlib.Path,
    target_file: pathlib.Path,
) -> list[pathlib.Path]:
    """Query Ninja for the target's object inputs."""
    build_directory = build_directory.resolve()
    if target_file.is_absolute():
        target = target_file.resolve().relative_to(build_directory)
    else:
        target = target_file

    result = subprocess.run(
        [
            str(ninja_executable),
            "-C",
            str(build_directory),
            "-t",
            "query",
            target.as_posix(),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise ValueError(
            f"ninja could not query {target}: {result.stderr.strip()}"
        )

    objects: list[pathlib.Path] = []
    reading_inputs = False
    for line in result.stdout.splitlines():
        entry = line.strip()
        if entry.startswith("input:"):
            reading_inputs = True
            continue
        if not reading_inputs:
            continue
        if entry.endswith(":") or entry.startswith(("outputs:", "validations:")):
            break
        path = pathlib.Path(entry)
        if path.suffix.lower() not in {".o", ".obj"}:
            continue
        if not path.is_absolute():
            path = build_directory / path
        path = path.resolve()
        if not path.is_file():
            raise FileNotFoundError(f"target link input is missing: {path}")
        if path not in objects:
            objects.append(path)
    return objects


def write_export_definition(output: pathlib.Path, export_text: str) -> None:
    """Write the module definition names used by the OS/2 CMake generators."""
    contents = (
        "LIBRARY vacuum INITINSTANCE TERMINSTANCE\n"
        "DESCRIPTION 'Vacuum shared utility library'\n"
        "EXPORTS\n"
        + export_text.rstrip()
        + "\n"
    )
    output.write_text(contents, encoding="utf-8")

    # Ninja's OS/2 linker rule uses the complete target filename as its base
    # (vacuum.dll.def), while Makefiles use the library stem (vacuum.def).
    if output.name.lower().endswith(".dll.def"):
        makefile_output = output.with_name(output.name[:-len(".dll.def")] + ".def")
        makefile_output.write_text(contents, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emxexp", required=True)
    parser.add_argument("--link-command-file")
    parser.add_argument("--working-directory")
    parser.add_argument("--ninja")
    parser.add_argument("--ninja-target")
    parser.add_argument("--build-directory")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    try:
        if args.ninja and args.ninja_target and args.build_directory:
            objects = collect_ninja_object_inputs(
                pathlib.Path(args.ninja),
                pathlib.Path(args.build_directory),
                pathlib.Path(args.ninja_target),
            )
        elif args.link_command_file and args.working_directory:
            working_directory = pathlib.Path(args.working_directory).resolve()
            objects = collect_object_inputs(
                pathlib.Path(args.link_command_file), working_directory
            )
        else:
            raise ValueError("specify either Ninja target details or link command file")
    except (OSError, ValueError) as error:
        print(f"cannot read target link inputs: {error}", file=sys.stderr)
        return 1

    if not objects:
        print("no OMF object inputs found", file=sys.stderr)
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

    write_export_definition(pathlib.Path(args.output), export_text)
    print(f"Generated OS/2 exports from {len(objects)} target object files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
