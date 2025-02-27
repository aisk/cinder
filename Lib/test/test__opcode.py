import dis
import opcode
from test.support import import_module
import unittest

_opcode = import_module("_opcode")
from _opcode import stack_effect


MISSING_STACK_EFFECT = {
    "CAST",
    "CONVERT_PRIMITIVE",
    "FAST_LEN",
    "PRIMITIVE_BINARY_OP",
    "PRIMITIVE_BOX",
    "INT_COMPARE_OP",
    "PRIMITIVE_LOAD_CONST",
    "INT_LOAD_CONST_OLD",
    "PRIMITIVE_UNARY_OP",
    "INT_UNBOX",
    "INVOKE_FUNCTION",
    "INVOKE_METHOD",
    "JUMP_IF_ZERO_OR_POP",
    "JUMP_IF_NONZERO_OR_POP",
    "LOAD_FIELD",
    "LOAD_ITERABLE_ARG",
    "LOAD_MAPPING_ARG",
    "LOAD_LOCAL",
    "LOAD_OBJ_FIELD",
    "POP_JUMP_IF_NONZERO",
    "POP_JUMP_IF_ZERO",
    "STORE_FIELD",
    "STORE_LOCAL",
    "STORE_OBJ_FIELD",
    "CHECK_ARGS",
    "BUILD_CHECKED_MAP",
    "SEQUENCE_GET",
    "SEQUENCE_SET",
    "SEQUENCE_REPEAT",
    "LIST_DEL",
    "REFINE_TYPE",
    "RETURN_INT",
}


class OpcodeTests(unittest.TestCase):

    def test_stack_effect(self):
        self.assertEqual(stack_effect(dis.opmap['POP_TOP']), -1)
        self.assertEqual(stack_effect(dis.opmap['DUP_TOP_TWO']), 2)
        self.assertEqual(stack_effect(dis.opmap['BUILD_SLICE'], 0), -1)
        self.assertEqual(stack_effect(dis.opmap['BUILD_SLICE'], 1), -1)
        self.assertEqual(stack_effect(dis.opmap['BUILD_SLICE'], 3), -2)
        self.assertRaises(ValueError, stack_effect, 30000)
        self.assertRaises(ValueError, stack_effect, dis.opmap['BUILD_SLICE'])
        self.assertRaises(ValueError, stack_effect, dis.opmap['POP_TOP'], 0)
        # All defined opcodes
        for name, code in dis.opmap.items():
            # TODO(T74641077) - Figure out how to deal with static python opcodes
            if name in MISSING_STACK_EFFECT or code in opcode.shadowop:
                continue

            with self.subTest(opname=name):
                if code < dis.HAVE_ARGUMENT:
                    stack_effect(code)
                    self.assertRaises(ValueError, stack_effect, code, 0)
                else:
                    stack_effect(code, 0)
                    self.assertRaises(ValueError, stack_effect, code)
        # All not defined opcodes
        for code in set(range(256)) - set(dis.opmap.values()):
            with self.subTest(opcode=code):
                self.assertRaises(ValueError, stack_effect, code)
                self.assertRaises(ValueError, stack_effect, code, 0)

    def test_stack_effect_jump(self):
        JUMP_IF_TRUE_OR_POP = dis.opmap['JUMP_IF_TRUE_OR_POP']
        self.assertEqual(stack_effect(JUMP_IF_TRUE_OR_POP, 0), 0)
        self.assertEqual(stack_effect(JUMP_IF_TRUE_OR_POP, 0, jump=True), 0)
        self.assertEqual(stack_effect(JUMP_IF_TRUE_OR_POP, 0, jump=False), -1)
        FOR_ITER = dis.opmap['FOR_ITER']
        self.assertEqual(stack_effect(FOR_ITER, 0), 1)
        self.assertEqual(stack_effect(FOR_ITER, 0, jump=True), -1)
        self.assertEqual(stack_effect(FOR_ITER, 0, jump=False), 1)
        JUMP_FORWARD = dis.opmap['JUMP_FORWARD']
        self.assertEqual(stack_effect(JUMP_FORWARD, 0), 0)
        self.assertEqual(stack_effect(JUMP_FORWARD, 0, jump=True), 0)
        self.assertEqual(stack_effect(JUMP_FORWARD, 0, jump=False), 0)
        # All defined opcodes
        has_jump = dis.hasjabs + dis.hasjrel
        for name, code in dis.opmap.items():
            # TODO(T74641077) - Figure out how to deal with static python opcodes
            if name in MISSING_STACK_EFFECT or code in opcode.shadowop:
                continue

            with self.subTest(opname=name):
                if code < dis.HAVE_ARGUMENT:
                    common = stack_effect(code)
                    jump = stack_effect(code, jump=True)
                    nojump = stack_effect(code, jump=False)
                else:
                    common = stack_effect(code, 0)
                    jump = stack_effect(code, 0, jump=True)
                    nojump = stack_effect(code, 0, jump=False)
                if code in has_jump:
                    self.assertEqual(common, max(jump, nojump))
                else:
                    self.assertEqual(jump, common)
                    self.assertEqual(nojump, common)


if __name__ == "__main__":
    unittest.main()
