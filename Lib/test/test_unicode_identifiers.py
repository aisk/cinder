import unittest

class PEP3131Test(unittest.TestCase):

    def test_valid(self):
        class T:
            ä = 1
            µ = 2 # this is a compatibility character
            蟒 = 3
            x󠄀 = 4
        self.assertEqual(getattr(T, "\xe4"), 1)
        self.assertEqual(getattr(T, "\u03bc"), 2)
        self.assertEqual(getattr(T, '\u87d2'), 3)
        self.assertEqual(getattr(T, 'x\U000E0100'), 4)

    @unittest.skipUnderCinderJIT("locals unavailable for JIT-compiled functions")
    def test_non_bmp_normalized(self):
        𝔘𝔫𝔦𝔠𝔬𝔡𝔢 = 1
        self.assertIn("Unicode", dir())

    def test_invalid(self):
        try:
            from test import badsyntax_3131
        except SyntaxError as s:
            self.assertEqual(str(s),
              "invalid character in identifier (badsyntax_3131.py, line 2)")
        else:
            self.fail("expected exception didn't occur")

if __name__ == "__main__":
    unittest.main()
