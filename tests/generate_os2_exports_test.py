#!/usr/bin/env python3
"""Regression tests for parsing OS/2 DLL link inputs."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SOURCE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE_ROOT / "src" / "utils"))

import generate_os2_exports  # noqa: E402


class GenerateOs2ExportsTest(unittest.TestCase):
    def test_writes_both_os2_linker_definition_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "vacuum.dll.def"

            generate_os2_exports.write_export_definition(output, "vacuum_symbol")

            makefile_output = output.with_name("vacuum.def")
            self.assertTrue(output.is_file())
            self.assertTrue(makefile_output.is_file())
            self.assertEqual(output.read_text(), makefile_output.read_text())

    def test_collects_ninja_target_object_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_directory = Path(temporary_directory)
            object_directory = (
                build_directory / "src" / "utils" / "CMakeFiles" / "vacuumu.dir"
            )
            object_directory.mkdir(parents=True)
            object_files = [
                object_directory / "action.cpp.o",
                object_directory / "options.cpp.o",
            ]
            for object_file in object_files:
                object_file.touch()

            query_output = (
                "src/utils/vacuum.dll:\n"
                "  input: CXX_SHARED_LIBRARY_LINKER__vacuumu_Release\n"
                "    src/utils/CMakeFiles/vacuumu.dir/action.cpp.o\n"
                "    src/utils/CMakeFiles/vacuumu.dir/options.cpp.o\n"
                "    /opt/os2/lib/Qt6Core.lib\n"
                "  outputs:\n"
                "    all\n"
            )
            completed = generate_os2_exports.subprocess.CompletedProcess(
                args=[], returncode=0, stdout=query_output, stderr=""
            )
            with patch.object(
                generate_os2_exports.subprocess, "run", return_value=completed
            ) as run:
                objects = generate_os2_exports.collect_ninja_object_inputs(
                    Path("ninja"),
                    build_directory,
                    build_directory / "src" / "utils" / "vacuum.dll",
                )

            self.assertEqual(objects, [path.resolve() for path in object_files])
            run.assert_called_once_with(
                [
                    "ninja",
                    "-C",
                    str(build_directory.resolve()),
                    "-t",
                    "query",
                    "src/utils/vacuum.dll",
                ],
                check=False,
                capture_output=True,
                text=True,
            )

    def test_collects_o_objects_from_cmake_response_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            object_directory = working_directory / "CMakeFiles" / "vacuumu.dir"
            object_directory.mkdir(parents=True)
            object_files = [
                object_directory / "action.cpp.o",
                object_directory / "mocs_compilation.cpp.o",
            ]
            for object_file in object_files:
                object_file.touch()

            response_file = object_directory / "objects1.rsp"
            response_file.write_text(
                "CMakeFiles/vacuumu.dir/action.cpp.o "
                "CMakeFiles/vacuumu.dir/mocs_compilation.cpp.o\n",
                encoding="utf-8",
            )
            link_command_file = object_directory / "link.txt"
            link_command_file.write_text(
                "os2-g++ @CMakeFiles/vacuumu.dir/objects1.rsp -lcx\n",
                encoding="utf-8",
            )

            objects = generate_os2_exports.collect_object_inputs(
                link_command_file, working_directory
            )

            self.assertEqual(objects, [path.resolve() for path in object_files])

    def test_collects_direct_obj_inputs_and_ignores_libraries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            object_file = working_directory / "direct.obj"
            object_file.touch()
            link_command_file = working_directory / "link.txt"
            link_command_file.write_text(
                "os2-g++ direct.obj library.a -lcx\n", encoding="utf-8"
            )

            objects = generate_os2_exports.collect_object_inputs(
                link_command_file, working_directory
            )

            self.assertEqual(objects, [object_file.resolve()])


if __name__ == "__main__":
    unittest.main()
