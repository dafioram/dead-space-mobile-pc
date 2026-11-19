"""Guard the actual GLES integration points; no game is started."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CoverageTests(unittest.TestCase):
    def test_both_draw_entries_capture_before_renderer_branch(self):
        source = (ROOT / "src/hle/gles.cpp").read_text(encoding="utf-8")
        for name, indexed in (("glDrawArrays", "false"), ("glDrawElements", "true")):
            with self.subTest(entry=name):
                body = source.split(f'reg("{name}",', 1)[1].split('\n    });', 1)[0]
                self.assertEqual(body.count("capture_draw("), 1)
                self.assertLess(body.index(f"capture_draw(c,{indexed},"), body.index("if (render::enabled())"))
                self.assertIn("return render::draw_", body)
                self.assertIn(f"{name}(", body)

    def test_capture_does_not_query_or_set_gl(self):
        source = (ROOT / "src/hle/gles.cpp").read_text(encoding="utf-8")
        body = source.split("void capture_draw(", 1)[1].split("// ---------------------------------------------------------------- texture decoding", 1)[0]
        self.assertFalse(re.search(r"\b(?:p_)?gl[A-Z]\w*\s*\(", body))
        self.assertIn("geometry_submit(input,observation_start)", body)
        native = (ROOT / "src/render/native.cpp").read_text(encoding="utf-8")
        self.assertNotIn("geometry_submit(", native, "must not double count Native forwarding")
        self.assertNotIn("geometry_draw(", native)

if __name__ == "__main__":
    unittest.main()
