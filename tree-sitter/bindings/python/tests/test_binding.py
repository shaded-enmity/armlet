from unittest import TestCase

import tree_sitter
import tree_sitter_armlet


class TestLanguage(TestCase):
    def test_can_load_grammar(self):
        try:
            tree_sitter.Language(tree_sitter_armlet.language())
        except Exception:
            self.fail("Error loading Armlet grammar")
