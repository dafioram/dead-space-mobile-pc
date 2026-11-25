"""Asset-free inline-SVC regression checks. Never loads a supplied game library."""
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
try:
    import unicorn as uc
    from unicorn import arm_const as arm
except ImportError:
    uc = None


class SolverInit(unittest.TestCase):
    def test_integration_and_guards(self):
        source = (ROOT / 'src/solver_init.cpp').read_text(encoding='utf-8')
        guards = [(int(a, 16), int(w, 16)) for a, w in re.findall(
            r'\{(0x[0-9a-f]+), (0x[0-9a-f]+)\}', source)]
        self.assertEqual(len(guards), 15)
        self.assertIn((0x26325c, 0xeb00a0a0), guards)
        self.assertIn((0x263254, 0xe3a01000), guards)  # fill value zero
        self.assertIn((0x263258, 0xe3a02088), guards)  # exact row length
        self.assertIn('hle::resolve("memset", true, true)', source)
        self.assertLess(source.index('for (const auto& g : guards)'), source.index('hle::resolve('))
        self.assertLess(source.index('invalid memset SVC'), source.index('mem::w32('))
        self.assertEqual(source.count('mem::w32('), 1)
        self.assertIn('mem::w32(mem::ELF_BASE + 0x26325c, svc)', source)
        self.assertNotIn('hook_entry(', source)
        self.assertNotIn('cpu::call(', source)
        patches = (ROOT / 'src/game_patches.cpp').read_text(encoding='utf-8')
        self.assertIn('solver::install_joint_row_clear();', patches)
        main = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
        self.assertLess(main.index('hle::register_all();'), main.index('patches::install();'))
        self.assertLess(main.index('patches::install();'), main.index('elf::run_init_array('))

    @unittest.skipIf(uc is None, 'optional unicorn emulator not installed')
    def test_inline_memset_svc(self):
        def execute(flags, inline):
            machine = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_ARM)
            machine.mem_map(0x10000, 0x2000)
            machine.mem_map(0x30000, 0x10000)
            machine.mem_write(0x30000, b'\xa5' * 0x10000)
            machine.reg_write(arm.UC_ARM_REG_CPSR, 0x10 | (flags << 28))
            if hasattr(arm, 'UC_ARM_REG_C1_C0_2'):
                machine.reg_write(arm.UC_ARM_REG_C1_C0_2, 0xf00000)
            machine.reg_write(arm.UC_ARM_REG_FPEXC, 1 << 30)
            for i in range(13):
                machine.reg_write(getattr(arm, 'UC_ARM_REG_R' + str(i)), 0x30000 + i * 0x100)
            machine.reg_write(arm.UC_ARM_REG_R6, 0x34004)
            machine.reg_write(arm.UC_ARM_REG_SP, 0x3f000)
            machine.reg_write(arm.UC_ARM_REG_LR, 0x12345678)
            for i in range(32):
                machine.reg_write(getattr(arm, 'UC_ARM_REG_S' + str(i)), 0x3f000000 + i)
            pack = lambda words: struct.pack('<' + 'I' * len(words), *words)
            # Synthetic intended call and direct SVC have identical effects. LR
            # is dead after the real site and redefined before consumption; model
            # that join, not a generic replacement of arbitrary BL instructions.
            code = [0xe1a00006, 0xe3a01000, 0xe3a02088,
                    0xef000007 if inline else 0xeb0003fb,
                    0xe3a0e000]
            machine.mem_write(0x10000, pack(code))
            machine.mem_write(0x11000, pack([0xef000007, 0xe12fff1e]))
            calls = []

            def memset_handler(_machine, _number, _data):
                # Existing HLE memset contract, without JIT or game execution.
                address = machine.reg_read(arm.UC_ARM_REG_R0)
                value = machine.reg_read(arm.UC_ARM_REG_R1) & 255
                size = machine.reg_read(arm.UC_ARM_REG_R2)
                calls.append((address, value, size))
                machine.mem_write(address, bytes([value]) * size)
                machine.reg_write(arm.UC_ARM_REG_R0, address)

            machine.hook_add(uc.UC_HOOK_INTR, memset_handler)
            machine.emu_start(0x10000, 0x10014, count=30)
            registers = [machine.reg_read(getattr(arm, 'UC_ARM_REG_R' + str(i))) for i in range(13)]
            registers += [machine.reg_read(arm.UC_ARM_REG_SP), machine.reg_read(arm.UC_ARM_REG_LR)]
            vfp = [machine.reg_read(getattr(arm, 'UC_ARM_REG_S' + str(i))) for i in range(32)]
            return registers, vfp, bytes(machine.mem_read(0x30000, 0x10000)), calls

        for flags in range(16):
            with self.subTest(flags=flags):
                reference, actual = execute(flags, False), execute(flags, True)
                self.assertEqual(reference, actual)
                self.assertEqual(actual[3], [(0x34004, 0, 136)])
                self.assertEqual(actual[2][0x4000:0x4004], b'\xa5' * 4)
                self.assertEqual(actual[2][0x4004:0x408c], bytes(136))
                self.assertEqual(actual[2][0x408c:0x4090], b'\xa5' * 4)


if __name__ == '__main__':
    unittest.main(verbosity=2)
