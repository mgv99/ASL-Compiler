//////////////////////////////////////////////////////////////////////
//
//    Asl - Another simple language (grammar)
//
//    Copyright (C) 2017  Universitat Politecnica de Catalunya
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU General Public License
//    as published by the Free Software Foundation; either version 3
//    of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Affero General Public License for more details.
//
//    You should have received a copy of the GNU Affero General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
//    contact: José Miguel Rivero (rivero@cs.upc.edu)
//             Computer Science Department
//             Universitat Politecnica de Catalunya
//             despatx Omega.110 - Campus Nord UPC
//             08034 Barcelona.  SPAIN
//
//////////////////////////////////////////////////////////////////////

grammar Asl;

//////////////////////////////////////////////////
/// Parser Rules
//////////////////////////////////////////////////

// A program is a list of functions
program : function+ EOF
        ;

// A function has a name, a list of parameters and a list of statements
function
        : FUNC ID '(' ( (ID ':' type) (',' ID ':' type)* )? ')' ( ':' type_ret)?
            declarations statements ENDFUNC
        ;

type_ret
        : basic_type
        ;

declarations
        : (variable_decl)*
        ;

variable_decl
        : VAR ID (',' ID)* ':' type
        ;


type
        : ARRAY '[' INTVAL ']' OF basic_type
        | basic_type
        ;

basic_type
        : INT
        | FLOAT
        | BOOL
        | CHAR
        ;

statements
        : (statement)*
        ;

// The different types of instructions
statement
          // Assignment
        : left_expr ASSIGN expr ';'                        # assignStmt
          // while-do-endwhile statement
        | WHILE expr DO statements ENDWHILE                # whileStmt
          // if-then-else statement (else is optional)
        | IF expr THEN statements (ELSE statements)? ENDIF # ifStmt
          // A function/procedure call has a list of arguments in parenthesis (possibly empty)
        | ident '(' ')' ';'                                # procCall
          // Read a variable
        | READ left_expr ';'                               # readStmt
          // Write an expression
        | WRITE expr ';'                                   # writeExpr
          // Write a string
        | WRITE STRING ';'                                 # writeString
          // Return statement
        | RETURN (expr)? ';'                               # returnStmt
        ;
// Grammar for left expressions (l-values in C++)
left_expr
        : ident
        | array_element
        ;

array_element
        : ident '[' expr ']'
        ;

// Grammar for expressions with boolean, relational and aritmetic operators
expr    : '(' expr ')'                        # parenthesisExpr

        | op=PLUS expr                        # arithmeticUnary
        | op=SUB expr                         # arithmeticUnary
        | op=NOT expr                         # booleanUnary

        | expr op=MUL expr                    # arithmetic
        | expr op=DIV expr                    # arithmetic
        | expr op=MOD expr                    # arithmetic

        | expr op=PLUS expr                   # arithmetic
        | expr op=SUB expr                    # arithmetic

        | expr op=EQUAL expr                  # relational
        | expr op=NEQUAL expr                 # relational
        | expr op=G expr                      # relational
        | expr op=L expr                      # relational
        | expr op=GEQ expr                    # relational
        | expr op=LEQ expr                    # relational

        | expr op=AND expr                    # booleanBinary
        | expr op=OR expr                     # booleanBinary

        | array_element                       # arrayValue
        | INTVAL                              # value
        | FLOATVAL                            # value
        | CHARVAL                             # value
        | BOOLVAL                             # value

        | ident '('(expr (',' expr)*)?')'     # procCallInExpr
        | ident                               # exprIdent
        ;

ident   : ID
        ;

//////////////////////////////////////////////////
/// Lexer Rules
//////////////////////////////////////////////////

AND       : 'and';
OR        : 'or' ;
NOT       : 'not';
ASSIGN    : '=' ;
EQUAL     : '==' ;
NEQUAL    : '!=' ;
G         : '>' ;
L         : '<' ;
GEQ       : '>=' ;
LEQ       : '<=' ;
PLUS      : '+' ;
SUB       : '-' ;
MUL       : '*';
DIV       : '/' ;
MOD       : '%' ;

VAR       : 'var';
INT       : 'int';
FLOAT     : 'float' ;
BOOL      : 'bool' ;
CHAR      : 'char' ;
ARRAY     : 'array' ;
OF        : 'of' ;

IF        : 'if' ;
THEN      : 'then' ;
ELSE      : 'else' ;
ENDIF     : 'endif' ;

WHILE     : 'while' ;
DO        : 'do' ;
ENDWHILE  : 'endwhile';

FUNC      : 'func' ;
ENDFUNC   : 'endfunc' ;

READ      : 'read' ;
WRITE     : 'write' ;
RETURN    : 'return' ;

INTVAL    : ('0'..'9')+ ;
FLOATVAL  : ('0'..'9')+ '.' ('0'..'9')+ ;
CHARVAL   : '\'' ('a'..'z'|'A'..'Z') '\'' ;
BOOLVAL   : ('true' | 'false') ; // mejor con TRUE | FALSE ?????

ID        : ('a'..'z'|'A'..'Z') ('a'..'z'|'A'..'Z'|'_'|'0'..'9')* ;


// Strings (in quotes) with escape sequences
STRING    : '"' ( ESC_SEQ | ~('\\'|'"') )* '"' ;

fragment
ESC_SEQ   : '\\' ('b'|'t'|'n'|'f'|'r'|'"'|'\''|'\\') ;

// Comments (inline C++-style)
COMMENT   : '//' ~('\n'|'\r')* '\r'? '\n' -> skip ;

// White spaces
WS        : (' '|'\t'|'\r'|'\n')+ -> skip ;
// Alternative description
// WS        : [ \t\r\n]+ -> skip ;
