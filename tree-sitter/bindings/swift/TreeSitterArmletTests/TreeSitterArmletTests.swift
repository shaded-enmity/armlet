import XCTest
import SwiftTreeSitter
import TreeSitterArmlet

final class TreeSitterArmletTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_armlet())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Armlet grammar")
    }
}
