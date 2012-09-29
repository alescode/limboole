# limboole

limboole is a tool for checking satisfiability and tautology on arbitrary logical formulas.

Check out the [project home](http://fmv.jku.at/limboole/) at the Institute for Formal Models and Verification.

# Overview

This is a simple boolean calculator.  It reads a boolean formula and checks
whether it is valid.  In case '-s' is specified satisfiability is checked
instead of validity.

# Language

The input format has the following syntax:
   expr ::= iff
   iff ::= implies [ <-> implies ]
   implies ::= or [ -> or ]
   or ::= and { | and }
   and ::= not { & not }
   not ::= basic | ! not
   basic ::= var | ( expr )

and 'var' is a string over letters, digits and the following characters:
  
  - _ . [ ] $ @

The last character of 'var' should be different from '-'.

# Install

Please get the 'limmat' SAT solver (version >= 1.2) and unpack it in the
same directory where you have unpacked 'limboole'.  Before you compile
'limboole' you have to generate 'liblimmat.a' in the 'limmat' subdirectory.
Then change to the 'limboole' subdirectory and issue 'make' to compile it.
This should also generate the test suite 'testlimboole'.  Run it to check
that everything works.

There is also a small utility 'dimacs2boole' that can be used to translate
CNF formulae in DIMACS format to the input format of 'limboole'.

Armin Biere, Computer Systems Institute, ETH Zurich
Die Nov  5 10:52:44 CET 2002
