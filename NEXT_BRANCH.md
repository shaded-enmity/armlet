# NEXT (development branch)

Goal: Resolve series 'e' issues

# NOTE on manual revision 'e'

The code in the master branch is configured against 'a' series of the manual, while
what can be downloaded now is actually 'e' series. The 'e' series of manual uses a 
different format for ARM32 instructions, but there is more:

- Encoding headers start with just A1, T1 ... An, Tn.
- Bit headers now have gaps that could be used to defer the length of the operands
- CONDITIONAL bit component is now written as '!= 1111'
- After the encoding information comes list of assembly variants
- Instructions now may span more than one page (plus the additional page with symbols, operations, etc.)
  (I have not checked ARM64 for this change yet)
- It seems that SYSTEM INSTRUCTIONS section was merged into standard instruction listing
  (I have not verified this yet, but I know that the sections that contained the instructions before are now gone)

# ROADMAP

[X] Implement multi-page instruction parsing for once
[X] Investigate where did the SYSTEM INSTRUCTIONS go
[X] Update the BIT COMPONENT parser to properly handle the '!= 1111' case
[X] Change BIT HEADER parsing semantics
 [X] Parse the gap information from BIT HEADERS and defer lengths of operands
[X] Encoding headers need to be parsed differently now
 [X] Also applies to dual encodings (A1/T1)
[X] Introduce types for BIT COMPONENTS
[O] Add data IMPORT/EXPORT capability
 [X] JSON (IMPORT/EXPORT)
 [O] LaTeX (EXPORT)
 [ ] SVG (EXPORT)
 [ ] C (EXPORT)
 [ ] Python (EXPORT)
 [ ] HTML (EXPORT)
[X] Parse assembly variants and link them with BIT COMPONENTS as needed
[X] Collect all PSEUDOCODE information
