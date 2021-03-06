//////////////////////////////////////////////////////////////////////
//
//    CodeGenVisitor - Walk the parser tree to do
//                     the generation of code
//
//    Copyright (C) 2019  Universitat Politecnica de Catalunya
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

#include "CodeGenVisitor.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/code.h"

#include <string>
#include <cstddef>    // std::size_t

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

// using namespace std;


// Constructor
CodeGenVisitor::CodeGenVisitor(TypesMgr       & Types,
                               SymTable       & Symbols,
                               TreeDecoration & Decorations) :
  Types{Types},
  Symbols{Symbols},
  Decorations{Decorations} {
}

// Methods to visit each kind of node:
//
antlrcpp::Any CodeGenVisitor::visitProgram(AslParser::ProgramContext *ctx) {
  DEBUG_ENTER();
  code my_code;
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  for (auto ctxFunc : ctx->function()) {
    subroutine subr = visit(ctxFunc);
    my_code.add_subroutine(subr);
  }
  Symbols.popScope();
  DEBUG_EXIT();
  return my_code;
}

antlrcpp::Any CodeGenVisitor::visitFunction(AslParser::FunctionContext *ctx) {
  DEBUG_ENTER();
  // NO SE TRATAN LOS ARGUMENTOS DE LA FUNCIÓN, ENTRE OTRAS COSAS...
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  subroutine subr(ctx->ID(0)->getText());
  if (ctx->ID(0)->getText() != "main") {
    subr.add_param("_result"); //en funciones void este param no será asignado, mirar visitReturnStmt
    int i = 1;
    while (ctx->ID(i)) {
      std::string pName = ctx->ID(i)->getText();
      subr.add_param(pName);
      ++i;
    }
  }
  codeCounters.reset();
  std::vector<var> && lvars = visit(ctx->declarations());
  for (auto & onevar : lvars) {
    subr.add_var(onevar);
  }
  instructionList && code = visit(ctx->statements());
  // ESTE RETURN ES NECESARIO? (YA VENIA PUESTO) [EN MAIN ES NECESARIO]
  code = code || instruction(instruction::RETURN());
  subr.set_instructions(code);
  Symbols.popScope();
  DEBUG_EXIT();
  return subr;
}

antlrcpp::Any CodeGenVisitor::visitDeclarations(AslParser::DeclarationsContext *ctx) {
  DEBUG_ENTER();
  std::vector<var> lvars;
  for (auto & varDeclCtx : ctx->variable_decl()) {
    std::vector<var> onevars = visit(varDeclCtx);
    //var onevar = visit(varDeclCtx);
    for (auto & onevar : onevars) {
      lvars.push_back(onevar);
    }
  }
  DEBUG_EXIT();
  return lvars;
}

antlrcpp::Any CodeGenVisitor::visitVariable_decl(AslParser::Variable_declContext *ctx) {
  DEBUG_ENTER();
  TypesMgr::TypeId   t1 = getTypeDecor(ctx->type());
  std::size_t      size = Types.getSizeOfType(t1);
  std::vector<var> lvars;
  for (auto & varID : ctx->ID()) {
    lvars.push_back(var{varID->getText(), size});
  }
  DEBUG_EXIT();
  return lvars;
}

antlrcpp::Any CodeGenVisitor::visitStatements(AslParser::StatementsContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  for (auto stCtx : ctx->statement()) {
    instructionList && codeS = visit(stCtx);
    code = code || codeS;
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitAssignStmt(AslParser::AssignStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE1 = visit(ctx->left_expr());
  std::string           addr1 = codAtsE1.addr;
  std::string           offs1 = codAtsE1.offs;
  instructionList &     code1 = codAtsE1.code;
  TypesMgr::TypeId t1 = getTypeDecor(ctx->left_expr());
  CodeAttribs     && codAtsE2 = visit(ctx->expr());
  std::string           addr2 = codAtsE2.addr;
  // std::string           offs2 = codAtsE2.offs;
  instructionList &     code2 = codAtsE2.code;
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr());
  code = code1 || code2;


  if (ctx->left_expr()->array_element()) {
    if (Types.isFloatTy(t1) and Types.isIntegerTy(t2)) {
      // floatTemp = float addr2
      // addr1[offs1] = floatTemp
      std::string floatTemp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(floatTemp, addr2) ||
             instruction::XLOAD(addr1, offs1, floatTemp);
    }
    else {
      // addr1[offs1] = addr2
      code = code || instruction::XLOAD(addr1, offs1, addr2);
    }
  }

  else if (Types.isArrayTy(t1) and Types.isArrayTy(t2)) {
    int arraySize = Types.getArraySize(t1);
    std::string offsTemp = "%"+codeCounters.newTEMP();
    std::string elemTemp = "%"+codeCounters.newTEMP();
    std::string addr1Temp = addr1;
    std::string addr2Temp = addr2;
    if (Symbols.isParameterClass(addr1)) {
      addr1Temp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::LOAD(addr1Temp, addr1); // temp = arrayIdent (NECESARIO)
    }
    if (Symbols.isParameterClass(addr2)) {
      addr2Temp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::LOAD(addr2Temp, addr2); // temp = arrayIdent (NECESARIO)
    }
    for (int i = 0; i < arraySize; ++i) { //copia por valor. ES POR REFERENCIA??? (sería asignar puntero)
      code = code ||
             instruction::ILOAD(offsTemp, std::to_string(i)) ||
             instruction::LOADX(elemTemp, addr2Temp, offsTemp) ||
             instruction::XLOAD(addr1Temp, offsTemp, elemTemp);
    }

  }
  else {
    if (Types.isFloatTy(t1) and Types.isIntegerTy(t2)) {
      // floatTemp = float addr2
      // addr1 = floatTemp
      std::string floatTemp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(floatTemp, addr2) ||
             instruction::LOAD(addr1, floatTemp);
    }
    else {
      // addr1 = addr2
      code = code || instruction::LOAD(addr1, addr2);
    }
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWhileStmt(AslParser::WhileStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE = visit(ctx->expr());
  std::string          addr1 = codAtsE.addr;
  instructionList &    code1 = codAtsE.code;
  instructionList &&   code2 = visit(ctx->statements()); // DO Statements
  std::string whileNum = codeCounters.newLabelWHILE();
  std::string labelWhile = "while"+whileNum;
  std::string labelEndWhile = "endwhile"+whileNum;

  code = instruction::LABEL(labelWhile) ||
         code1 ||
         instruction::FJUMP(addr1, labelEndWhile) ||
         code2 ||
         instruction::UJUMP(labelWhile) ||
         instruction::LABEL(labelEndWhile);

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitIfStmt(AslParser::IfStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  CodeAttribs     && codAtsE = visit(ctx->expr());
  std::string          addr1 = codAtsE.addr;
  instructionList &    code1 = codAtsE.code;
  instructionList &&   code2 = visit(ctx->statements(0)); // THEN Statements
  //instructionList &&   code3 = visit(ctx->statements(1)); // ELSE Statements
  std::string label = codeCounters.newLabelIF();
  std::string labelEndIf = "endif"+label;
  std::string labelElse = "else"+label;
  if (ctx->ELSE()) {
    instructionList &&   code3 = visit(ctx->statements(1)); // ELSE Statements
    code = code1 || // code IF
           instruction::FJUMP(addr1, labelElse) ||
           code2 || // code THEN
           instruction::UJUMP(labelEndIf) ||
           instruction::LABEL(labelElse) ||
           code3 || // code ELSE
           instruction::LABEL(labelEndIf);
  }
  else {
    code = code1 ||
           instruction::FJUMP(addr1, labelEndIf) ||
           code2 ||
           instruction::LABEL(labelEndIf);
  }

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitProcCall(AslParser::ProcCallContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  // std::string name = ctx->ident()->ID()->getSymbol()->getText();
  std::string name = ctx->ident()->getText();
  TypesMgr::TypeId tFunc = getTypeDecor(ctx->ident());

  code = code || instruction::PUSH(); //Push espacio para return

  int i = 0;
  while (ctx->expr(i)) {
    CodeAttribs     && codAt1 = visit(ctx->expr(i));
    std::string         addr1 = codAt1.addr;
    instructionList &   code1 = codAt1.code;
    code = code || code1;
    TypesMgr::TypeId tExpr = getTypeDecor(ctx->expr(i));
    TypesMgr::TypeId tParam = Types.getParameterType(tFunc, i);
    if (Types.isIntegerTy(tExpr) and Types.isFloatTy(tParam)) {
      // coercion float -> int
      std::string floatTemp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(floatTemp, addr1) ||
             instruction::PUSH(floatTemp);
    }
    else {
      if (Types.isArrayTy(tExpr)) {
        // push array reference
        std::string refTemp = "%"+codeCounters.newTEMP();
        code = code ||
               instruction::ALOAD(refTemp, addr1) ||
               instruction::PUSH(refTemp);
      }
      else {
        // push normal
        code = code || instruction::PUSH(addr1);
      }
    }
    ++i;
  }
  code = code || instruction::CALL(name);
  while (i > 0) {
    code = code || instruction::POP();
    i--;
  }
  code = code || instruction::POP(); // POP return (se ignora porque no es assignment, y puede ser void)

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitReadStmt(AslParser::ReadStmtContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAtsE = visit(ctx->left_expr());
  std::string          addr1 = codAtsE.addr;
  std::string          offs1 = codAtsE.offs;
  instructionList &    code1 = codAtsE.code;

  instructionList &     code = code1;
  TypesMgr::TypeId tid1 = getTypeDecor(ctx->left_expr());
  std::string temp = addr1;
  if (ctx->left_expr()->array_element()) {
    temp = "%"+codeCounters.newTEMP();
  }
  if (Types.isIntegerTy(tid1) or Types.isBooleanTy(tid1))
    code = code || instruction::READI(temp);
  else if (Types.isFloatTy(tid1))
    code = code || instruction::READF(temp);
  else if (Types.isCharacterTy(tid1))
    code = code || instruction::READC(temp);

  if (ctx->left_expr()->array_element()) {
    // addr1[offs1] = temp
    code = code ||
           instruction::XLOAD(addr1, offs1, temp);
  }

  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWriteExpr(AslParser::WriteExprContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr());
  std::string         addr1 = codAt1.addr;
  // std::string         offs1 = codAt1.offs;
  instructionList &   code1 = codAt1.code;
  instructionList &    code = code1;
  TypesMgr::TypeId tExpr = getTypeDecor(ctx->expr());
  if (Types.isIntegerTy(tExpr) or Types.isBooleanTy(tExpr))
    code = code1 || instruction::WRITEI(addr1);
  else if (Types.isFloatTy(tExpr))
    code = code1 || instruction::WRITEF(addr1);
  else if (Types.isCharacterTy(tExpr))
    code = code1 || instruction::WRITEC(addr1);
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitWriteString(AslParser::WriteStringContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string s = ctx->STRING()->getText();
  std::string temp = "%"+codeCounters.newTEMP();
  int i = 1;
  while (i < int(s.size())-1) {
    if (s[i] != '\\') {
      code = code ||
	     instruction::CHLOAD(temp, s.substr(i,1)) ||
	     instruction::WRITEC(temp);
      i += 1;
    }
    else {
      assert(i < int(s.size())-2);
      if (s[i+1] == 'n') {
        code = code || instruction::WRITELN();
        i += 2;
      }
      else if (s[i+1] == 't' or s[i+1] == '"' or s[i+1] == '\\') {
        code = code ||
               instruction::CHLOAD(temp, s.substr(i,2)) ||
	       instruction::WRITEC(temp);
        i += 2;
      }
      else {
        code = code ||
               instruction::CHLOAD(temp, s.substr(i,1)) ||
	       instruction::WRITEC(temp);
        i += 1;
      }
    }
  }
  DEBUG_EXIT();
  return code;
}

antlrcpp::Any CodeGenVisitor::visitLeft_expr(AslParser::Left_exprContext *ctx) {
  DEBUG_ENTER();
  if (ctx->ident()) {
    CodeAttribs && codAts = visit(ctx->ident());
    DEBUG_EXIT();
    return codAts;
  }
  else { // array_element
    std::string arrayIdent = ctx->array_element()->ident()->getText();
    CodeAttribs     && codAtExpr = visit(ctx->array_element()->expr());
    std::string         addrExpr = codAtExpr.addr;
    instructionList &   codeExpr = codAtExpr.code;
    instructionList code;
    std::string temp = arrayIdent;
    code = codeExpr;
    if (Symbols.isParameterClass(arrayIdent)) {
      temp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::LOAD(temp, arrayIdent); // temp = arrayIdent (NECESARIO)
    }
    CodeAttribs codAts(temp, addrExpr, code);
    DEBUG_EXIT();
    return codAts;
  }
}


antlrcpp::Any CodeGenVisitor::visitParenthesisExpr(AslParser::ParenthesisExprContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->expr());
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitArithmeticUnary(AslParser::ArithmeticUnaryContext *ctx) {
  CodeAttribs     && codAt1 = visit(ctx->expr());
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  instructionList &&   code = code1 || instructionList();

  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  //TypesMgr::TypeId  t = getTypeDecor(ctx);

  std::string temp = "%"+codeCounters.newTEMP();
  if (ctx->PLUS())
    temp = addr1;
  else if (ctx->SUB()) {
    if (Types.isIntegerTy(t1)) {
      code = code || instruction::NEG(temp, addr1);
    }
    else if (Types.isFloatTy(t1)) {
      code = code || instruction::FNEG(temp, addr1);
    }
  }

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitBooleanUnary(AslParser::BooleanUnaryContext *ctx) {
  CodeAttribs     && codAt1 = visit(ctx->expr());
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  instructionList &&   code = code1 || instructionList();
  // TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  // TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();
  if (ctx->NOT())
    code = code || instruction::NOT(temp, addr1);

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitArithmeticBinary(AslParser::ArithmeticBinaryContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;

  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  // TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();
  if (Types.isIntegerTy(t1) and Types.isIntegerTy(t2)) {
    if (ctx->MUL())
      code = code || instruction::MUL(temp, addr1, addr2);
    else if (ctx->DIV())
      code = code || instruction::DIV(temp, addr1, addr2);
    else if (ctx->PLUS())
      code = code || instruction::ADD(temp, addr1, addr2);
    else if (ctx->SUB())
      code = code || instruction::SUB(temp, addr1, addr2);
    else if (ctx->MOD()) {
      std::string divTemp = "%"+codeCounters.newTEMP();
      std::string mulTemp = "%"+codeCounters.newTEMP();
      code = code || instruction::DIV(divTemp, addr1, addr2) ||
             instruction::MUL(mulTemp, divTemp, addr2) ||
             instruction::SUB(temp, addr1, mulTemp);
    }
  }
  else { // Some float
    std::string temp1 = addr1;
    std::string temp2 = addr2;
    if (Types.isIntegerTy(t1)) {
      temp1 = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(temp1, addr1);
    }
    else if (Types.isIntegerTy(t2)) {
      temp2 = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(temp2, addr2);
    }
    if (ctx->MUL())
      code = code || instruction::FMUL(temp, temp1, temp2);
    else if (ctx->DIV())
      code = code || instruction::FDIV(temp, temp1, temp2);
    else if (ctx->PLUS())
      code = code || instruction::FADD(temp, temp1, temp2);
    else if (ctx->SUB())
      code = code || instruction::FSUB(temp, temp1, temp2);
  }

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitRelational(AslParser::RelationalContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  // TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();
  if (Types.isIntegerTy(t1) and Types.isIntegerTy(t2)) {
    if (ctx->EQUAL())
      code = code || instruction::EQ(temp, addr1, addr2);
    else if (ctx->NEQUAL())
      code = code || instruction::EQ(temp, addr1, addr2) || instruction::NOT(temp, temp);
    else if (ctx->G())
      code = code || instruction::LT(temp, addr2, addr1);
    else if (ctx->L())
      code = code || instruction::LT(temp, addr1, addr2);
    else if (ctx->GEQ())
      code = code || instruction::LE(temp, addr2, addr1);
    else if (ctx->LEQ())
      code = code || instruction::LE(temp, addr1, addr2);
  }
  else { // Some float
    std::string temp1 = addr1;
    std::string temp2 = addr2;
    if (Types.isIntegerTy(t1)) {
      temp1 = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(temp1, addr1);
    }
    else if (Types.isIntegerTy(t2)) {
      temp2 = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(temp2, addr2);
    }
    if (ctx->EQUAL())
      code = code || instruction::FEQ(temp, temp1, temp2);
    else if (ctx->NEQUAL())
      code = code || instruction::FEQ(temp, temp1, temp2) || instruction::NOT(temp, temp);
    else if (ctx->G())
      code = code || instruction::FLT(temp, temp2, temp1);
    else if (ctx->L())
      code = code || instruction::FLT(temp, temp1, temp2);
    else if (ctx->GEQ())
      code = code || instruction::FLE(temp, temp2, temp1);
    else if (ctx->LEQ())
      code = code || instruction::FLE(temp, temp1, temp2);
  }
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitBooleanBinary(AslParser::BooleanBinaryContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs     && codAt1 = visit(ctx->expr(0));
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  CodeAttribs     && codAt2 = visit(ctx->expr(1));
  std::string         addr2 = codAt2.addr;
  instructionList &   code2 = codAt2.code;
  instructionList &&   code = code1 || code2;
  // TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  // TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  // TypesMgr::TypeId  t = getTypeDecor(ctx);
  std::string temp = "%"+codeCounters.newTEMP();
  if (ctx->AND())
    code = code || instruction::AND(temp, addr1, addr2);
  else if (ctx->OR())
    code = code || instruction::OR(temp, addr1, addr2);

  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitArrayValue(AslParser::ArrayValueContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string arrayIdent = ctx->array_element()->ident()->getText();
  CodeAttribs     && codAt1 = visit(ctx->array_element()->expr());
  std::string         addr1 = codAt1.addr;
  instructionList &   code1 = codAt1.code;
  std::string temp = "%"+codeCounters.newTEMP();
  std::string arrayTemp = arrayIdent;
  code = code1;
  if (Symbols.isParameterClass(arrayIdent)) {
    arrayTemp = "%"+codeCounters.newTEMP();
    code = code ||
           instruction::LOAD(arrayTemp, arrayIdent); // temp = arrayIdent (NECESARIO)
  }
  code = code ||
         instruction::LOADX(temp, arrayTemp, addr1);
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitValue(AslParser::ValueContext *ctx) {
  DEBUG_ENTER();
  instructionList code;
  std::string temp = "%"+codeCounters.newTEMP();
  if (ctx->INTVAL())
    code = instruction::ILOAD(temp, ctx->getText());
  else if (ctx->FLOATVAL())
    code = instruction::FLOAD(temp, ctx->getText());
  else if (ctx->BOOLVAL() and ctx->getText()=="true")
    code = instruction::ILOAD(temp, "1");
  else if (ctx->BOOLVAL() and ctx->getText()=="false")
    code = instruction::ILOAD(temp, "0");
  else if (ctx->CHARVAL()) {
    std::string charval = ctx->getText();
    if (charval.length() == 3) { // chars normales. e.g. 'a'
      code = instruction::CHLOAD(temp, charval.substr(1,1));
    }
    else { // chars "compuestos". e.g. '\n'
        code = instruction::CHLOAD(temp, charval.substr(1,2));
    }
  }
  CodeAttribs codAts(temp, "", code);
  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitProcCallInExpr(AslParser::ProcCallInExprContext *ctx) {
  DEBUG_ENTER();
  instructionList code = instructionList();
  // std::string name = ctx->ident()->ID()->getSymbol()->getText();
  std::string funcName = ctx->ident()->getText();
  TypesMgr::TypeId tFunc = getTypeDecor(ctx->ident());

  code = code || instruction::PUSH(); //Push espacio para return
  int i = 0;
  while (ctx->expr(i)) {
    CodeAttribs     && codAt1 = visit(ctx->expr(i));
    std::string         addr1 = codAt1.addr;
    instructionList &   code1 = codAt1.code;
    code = code || code1;
    TypesMgr::TypeId tExpr = getTypeDecor(ctx->expr(i));
    TypesMgr::TypeId tParam = Types.getParameterType(tFunc, i);
    if (Types.isIntegerTy(tExpr) and Types.isFloatTy(tParam)) {
      std::string floatTemp = "%"+codeCounters.newTEMP();
      code = code ||
             instruction::FLOAT(floatTemp, addr1) ||
             instruction::PUSH(floatTemp);
    }
    else {
      if (Types.isArrayTy(tExpr)) {
        // push array reference
        std::string refTemp = "%"+codeCounters.newTEMP();
        code = code ||
               instruction::ALOAD(refTemp, addr1) ||
               instruction::PUSH(refTemp);
      }
      else {
        // push normal
        code = code || instruction::PUSH(addr1);
      }
    }
    ++i;
  }
  code = code || instruction::CALL(funcName);
  while (i > 0) {
    code = code || instruction::POP();
    i--;
  }
  std::string temp = "%"+codeCounters.newTEMP();
  code = code || instruction::POP(temp); // POP return
  CodeAttribs codAts(temp, "", code);

  DEBUG_EXIT();
  return codAts;
}

antlrcpp::Any CodeGenVisitor::visitExprIdent(AslParser::ExprIdentContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs && codAts = visit(ctx->ident());
  DEBUG_EXIT();
  return codAts;
}


antlrcpp::Any CodeGenVisitor::visitReturnStmt(AslParser::ReturnStmtContext *ctx) {
  DEBUG_ENTER();
  instructionList code = instructionList();
  if (ctx->expr()) {
    CodeAttribs     && codAt1 = visit(ctx->expr());
    std::string         addr1 = codAt1.addr;
    instructionList &   code1 = codAt1.code;
    code = code || code1 ||
    instruction::LOAD("_result", addr1) ||
    instruction::RETURN();

  }
  else {
    code = instruction::RETURN();
  }
  DEBUG_EXIT();
  return code;
}


antlrcpp::Any CodeGenVisitor::visitIdent(AslParser::IdentContext *ctx) {
  DEBUG_ENTER();
  CodeAttribs codAts(ctx->ID()->getText(), "", instructionList());
  DEBUG_EXIT();
  return codAts;
}


// Getters for the necessary tree node atributes:
//   Scope and Type
SymTable::ScopeId CodeGenVisitor::getScopeDecor(antlr4::ParserRuleContext *ctx) const {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId CodeGenVisitor::getTypeDecor(antlr4::ParserRuleContext *ctx) const {
  return Decorations.getType(ctx);
}


// Constructors of the class CodeAttribs:
//
CodeGenVisitor::CodeAttribs::CodeAttribs(const std::string & addr,
					 const std::string & offs,
					 instructionList & code) :
  addr{addr}, offs{offs}, code{code} {
}

CodeGenVisitor::CodeAttribs::CodeAttribs(const std::string & addr,
					 const std::string & offs,
					 instructionList && code) :
  addr{addr}, offs{offs}, code{code} {
}
