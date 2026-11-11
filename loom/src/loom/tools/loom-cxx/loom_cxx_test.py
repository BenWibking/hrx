# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Compiles tiny single-source translation units with loom-cxx and checks the
objects: host code, the embedded device code object with its registration
constructor, and the kernel descriptors, all from one parse of the source."""

import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ARGS = None

EM_X86_64, EM_AARCH64 = 62, 183
HOST_MACHINES = (EM_X86_64, EM_AARCH64)
STB_GLOBAL, STB_WEAK = 1, 2
STT_OBJECT, STT_FUNC = 1, 2
SHN_UNDEF = 0

KERNEL_PRELUDE = r"""
#include <loomcxx/kernel.h>
static loom::kernel::configuration launch_config(unsigned groups) {
  return {{groups, 1, 1}, {64, 1, 1}};
}
template <class T>
[[loom::kernel(launch_config), loom::kernel_type_symbol]]
void run(T tag) { tag(); }
struct Body {
  float a;
  float* y;
  [[loom::inline]] void operator()() const { y[0] = a; }
};
"""

BODY_REF = "_ZN4loom6kernel10kernel_refIXadL_Z3runI4BodyEvT_EEEE"
BAD_REF = "_ZN4loom6kernel10kernel_refIXadL_Z3runI3BadEvT_EEEE"


class Elf:
    """Minimal little-endian ELF64 relocatable object reader."""

    def __init__(self, data):
        assert data[:4] == b"\x7fELF" and data[4] == 2, "not an ELF64 object"
        self.data = data
        (shoff,) = struct.unpack_from("<Q", data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
        self.sections = []
        for i in range(shnum):
            fields = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
            self.sections.append(fields)
        names = self.sections[shstrndx]
        self.names = [self._string(names, s[0]) for s in self.sections]

    def _string(self, table, offset):
        start = table[4] + offset
        return self.data[start : self.data.index(b"\0", start)].decode()

    def section(self, name):
        for section_name, section in zip(self.names, self.sections):
            if section_name == name:
                return section
        return None

    def contents(self, section):
        return self.data[section[4] : section[4] + section[5]]

    def symbols(self):
        symtab = self.section(".symtab")
        strtab = self.sections[symtab[6]]
        result = {}
        for offset in range(0, symtab[5], 24):
            name, info, _, shndx, _, _ = struct.unpack_from(
                "<IBBHQQ", self.data, symtab[4] + offset
            )
            if name:
                result[self._string(strtab, name)] = (info >> 4, info & 15, shndx)
        return result


class LoomCxxTest(unittest.TestCase):
    def compile(self, source, *extra, expect_success=True):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        directory = pathlib.Path(temporary.name)
        source_path = directory / "unit.cpp"
        source_path.write_text(source)
        object_path = directory / "unit.o"
        command = [
            ARGS.loom_cxx,
            "-c",
            str(source_path),
            "-o",
            str(object_path),
            "-std=gnu++17",
            "-O2",
            "-isystem",
            ARGS.include,
            "-Wall",
            "-fPIC",
            "-MD",
            "-MT",
            str(object_path),
            "-MF",
            str(directory / "unit.d"),
            f"--loom-target={ARGS.target}",
            "--loom-register-function=test_register_code_object",
            "--loom-stats",
            *extra,
        ]
        run = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        if expect_success:
            self.assertEqual(run.returncode, 0, run.stderr)
            elf = Elf(object_path.read_bytes())
            depfile = (directory / "unit.d").read_text()
            return elf, run.stderr, depfile
        self.assertNotEqual(run.returncode, 0, run.stderr)
        self.assertFalse(object_path.exists())
        return None, run.stderr, None

    def device_code_objects(self, elf):
        """Machines of the ELF code objects embedded in read-only data."""
        found = []
        for name, section in zip(elf.names, elf.sections):
            if not name.startswith(".rodata") and not name.startswith(".lrodata"):
                continue
            contents = elf.contents(section)
            start = contents.find(b"\x7fELF")
            while start >= 0:
                (machine,) = struct.unpack_from("<H", contents, start + 18)
                if machine not in HOST_MACHINES:
                    found.append(machine)
                start = contents.find(b"\x7fELF", start + 1)
        return found

    def test_kernel_unit_is_parsed_once_into_one_object(self):
        elf, stderr, depfile = self.compile(
            KERNEL_PRELUDE
            + """
const loom::kernel::descriptor* host_entry() {
  return &loom::kernel::kernel_ref<&run<Body>>;
}
"""
        )
        # One preprocess and one parse fed host code generation and import.
        self.assertIn("preprocess=1 parse=1", stderr)
        self.assertIn("host_codegen=1 imports=1 device_compiles=1", stderr)
        symbols = elf.symbols()
        # Host code.
        binding, kind, shndx = symbols["_Z10host_entryv"]
        self.assertEqual((binding, kind), (STB_GLOBAL, STT_FUNC))
        self.assertNotEqual(shndx, SHN_UNDEF)
        # The kernel descriptor: a weak definition, available.
        binding, kind, shndx = symbols[BODY_REF]
        self.assertEqual((binding, kind), (STB_WEAK, STT_OBJECT))
        self.assertNotEqual(shndx, SHN_UNDEF)
        descriptor = elf.contents(elf.sections[shndx])
        self.assertEqual(struct.unpack_from("<II", descriptor, 0), (1, 1))
        # The embedded device code object and its registration constructor.
        self.assertTrue(self.device_code_objects(elf))
        self.assertGreater(elf.section(".init_array")[5], 0)
        self.assertEqual(symbols["test_register_code_object"][2], SHN_UNDEF)
        # No device-only symbols reach the host object.
        self.assertFalse(
            [s for s in symbols if s.startswith("_ZN4loom") and s != BODY_REF]
        )
        self.assertIn("loomcxx/kernel.h", depfile)

    def test_host_only_unit_has_no_device_pass(self):
        elf, stderr, _ = self.compile("int host_only(int x) { return x + 1; }\n")
        self.assertIn("preprocess=1 parse=1", stderr)
        self.assertIn("imports=0 device_compiles=0", stderr)
        self.assertIsNone(elf.section(".init_array"))
        self.assertEqual(self.device_code_objects(elf), [])
        self.assertIn("_Z9host_onlyi", elf.symbols())

    def test_dropped_kernels_keep_unavailable_descriptors(self):
        source = (
            KERNEL_PRELUDE
            + """
struct Bad {
  float* y;
  [[loom::inline]] void operator()() const { if (y != nullptr) y[0] = 2.0f; }
};
const loom::kernel::descriptor* good() {
  return &loom::kernel::kernel_ref<&run<Body>>;
}
const loom::kernel::descriptor* bad() {
  return &loom::kernel::kernel_ref<&run<Bad>>;
}
"""
        )
        # Without the drop policy the unsupported kernel fails the compile.
        self.compile(source, expect_success=False)
        elf, stderr, _ = self.compile(source, "--loom-drop-failed-kernels")
        self.assertIn("dropped 1 launched kernel(s)", stderr)
        self.assertIn("  dropped: ", stderr)
        self.assertIn("preprocess=1 parse=1", stderr)
        symbols = elf.symbols()
        for symbol, available in ((BODY_REF, 1), (BAD_REF, 0)):
            _, _, shndx = symbols[symbol]
            descriptor = elf.contents(elf.sections[shndx])
            self.assertEqual(struct.unpack_from("<II", descriptor, 0), (1, available))
        self.assertTrue(self.device_code_objects(elf))

    def test_host_errors_are_compile_errors(self):
        _, stderr, _ = self.compile(
            "int broken() { return undeclared_name; }\n", expect_success=False
        )
        self.assertIn("undeclared_name", stderr)
        self.assertIn("error", stderr)
        self.assertNotIn("deferred error", stderr)


def main():
    global ARGS
    parser = argparse.ArgumentParser()
    parser.add_argument("--loom-cxx", required=True)
    parser.add_argument("--include", required=True)
    parser.add_argument("--target", required=True)
    ARGS, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *rest])


if __name__ == "__main__":
    main()
