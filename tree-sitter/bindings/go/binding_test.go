package tree_sitter_armlet_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_armlet "github.com/tree-sitter/tree-sitter-armlet/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_armlet.Language())
	if language == nil {
		t.Errorf("Error loading Armlet grammar")
	}
}
