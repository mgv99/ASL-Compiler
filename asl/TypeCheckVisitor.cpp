//////////////////////////////////////////////////////////////////////
//
//    TypeCheckVisitor - Walk the parser tree to do the semantic
//                       typecheck for the Asl programming language
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


#include "TypeCheckVisitor.h"

#include "antlr4-runtime.h"

#include "../common/TypesMgr.h"
#include "../common/SymTable.h"
#include "../common/TreeDecoration.h"
#include "../common/SemErrors.h"

#include <iostream>
#include <string>

// uncomment the following line to enable debugging messages with DEBUG*
// #define DEBUG_BUILD
#include "../common/debug.h"

// using namespace std;


// Constructor
TypeCheckVisitor::TypeCheckVisitor(TypesMgr       & Types,
				   SymTable       & Symbols,
				   TreeDecoration & Decorations,
				   SemErrors      & Errors) :
  Types{Types},
  Symbols {Symbols},
  Decorations{Decorations},
  Errors{Errors} {
}

// Methods to visit each kind of node:
//
antlrcpp::Any TypeCheckVisitor::visitProgram(AslParser::ProgramContext *ctx) {
  DEBUG_ENTER();
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  for (auto ctxFunc : ctx->function()) {
    visit(ctxFunc);
  }
  if (Symbols.noMainProperlyDeclared())
    Errors.noMainProperlyDeclared(ctx);
  Symbols.popScope();
  Errors.print();
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitFunction(AslParser::FunctionContext *ctx) {
  DEBUG_ENTER();
  SymTable::ScopeId sc = getScopeDecor(ctx);
  Symbols.pushThisScope(sc);
  //Symbols.print();
	TypesMgr::TypeId tRet;
	if (ctx->type_ret()) {
		//visit(ctx->type_ret());
		tRet = getTypeDecor(ctx->type_ret());
	}
	else {
		tRet = Types.createVoidTy();
	}
	// VER SI ESTO SE PUEDE HACER DE OTRA MANERA, SIN MIRAR TYPE_RET (ES RARO PQ TYPE_RET SE VISITA EN SYMBOLSVISITOR)
	Symbols.setCurrentFunctionTy(tRet);

  visit(ctx->statements());
  Symbols.popScope();
  DEBUG_EXIT();
  return 0;
}

// antlrcpp::Any TypeCheckVisitor::visitDeclarations(AslParser::DeclarationsContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any TypeCheckVisitor::visitVariable_decl(AslParser::Variable_declContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

// antlrcpp::Any TypeCheckVisitor::visitType(AslParser::TypeContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

antlrcpp::Any TypeCheckVisitor::visitStatements(AslParser::StatementsContext *ctx) {
  DEBUG_ENTER();
  visitChildren(ctx);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitAssignStmt(AslParser::AssignStmtContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->left_expr());
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->left_expr());
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isErrorTy(t2)) and
      (not Types.copyableTypes(t1, t2)))
    Errors.incompatibleAssignment(ctx->ASSIGN());
  if ((not Types.isErrorTy(t1)) and (not getIsLValueDecor(ctx->left_expr())))
    Errors.nonReferenceableLeftExpr(ctx->left_expr());
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitIfStmt(AslParser::IfStmtContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1)))
    Errors.booleanRequired(ctx);
  visit(ctx->statements(0)); //THEN Statements
	if (ctx->statements(1))
		visit(ctx->statements(1)); //ELSE Statements
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitWhileStmt(AslParser::WhileStmtContext *ctx) {
	DEBUG_ENTER();
	visit(ctx->expr());
	TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
	if ((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1)))
		Errors.booleanRequired(ctx);
	visit(ctx->statements());
	DEBUG_EXIT();
	return 0;
}

antlrcpp::Any TypeCheckVisitor::visitReturnStmt(AslParser::ReturnStmtContext *ctx) {
	DEBUG_ENTER();
	TypesMgr::TypeId retTy;
	if (ctx->expr()) {
		visit(ctx->expr());
		retTy = getTypeDecor(ctx->expr());
	}
	else {
		retTy = Types.createVoidTy();
	}
	TypesMgr::TypeId funcTy = Symbols.getCurrentFunctionTy();
	//std::cout << Types.to_string(funcTy) << std::endl;
	//std::cout << Types.to_string(retTy) << std::endl;
	//std::cout << "================" << std::endl;

	if (not Types.copyableTypes(funcTy, retTy)) {
		Errors.incompatibleReturn(ctx->RETURN());
	}
	DEBUG_EXIT();
	return 0;
}

antlrcpp::Any TypeCheckVisitor::visitProcCall(AslParser::ProcCallContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->ident());
  TypesMgr::TypeId identTy = getTypeDecor(ctx->ident());
  if (not Types.isFunctionTy(identTy) and not Types.isErrorTy(identTy)) {
    Errors.isNotCallable(ctx->ident());
  }
	int numParams = 0;
	if (Types.isFunctionTy(identTy))
		numParams = Types.getNumOfParameters(identTy);
	int i = 0;
	// check los parametros tienen el tipo adecuado
	while (ctx->expr(i)) {
		visit(ctx->expr(i));
		if (i < numParams) {
			TypesMgr::TypeId tExpr = getTypeDecor(ctx->expr(i));
			TypesMgr::TypeId tParam = Types.getParameterType(identTy, i);
			if ((not Types.isErrorTy(tExpr)) and (not Types.copyableTypes(tParam, tExpr)))
				Errors.incompatibleParameter(ctx->expr(i), i+1, ctx);
		}
		++i;
	}
	// check numero de parametros correcto
	if (Types.isFunctionTy(identTy) and (numParams != i)) {
			Errors.numberOfParameters(ctx->ident());
	}
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitReadStmt(AslParser::ReadStmtContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->left_expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->left_expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isPrimitiveTy(t1)) and
      (not Types.isFunctionTy(t1)))
    Errors.readWriteRequireBasic(ctx);
  if ((not Types.isErrorTy(t1)) and (not getIsLValueDecor(ctx->left_expr())))
    Errors.nonReferenceableExpression(ctx);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitWriteExpr(AslParser::WriteExprContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->expr());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
  if ((not Types.isErrorTy(t1)) and (not Types.isPrimitiveTy(t1)))
    Errors.readWriteRequireBasic(ctx);
  DEBUG_EXIT();
  return 0;
}

// antlrcpp::Any TypeCheckVisitor::visitWriteString(AslParser::WriteStringContext *ctx) {
//   DEBUG_ENTER();
//   antlrcpp::Any r = visitChildren(ctx);
//   DEBUG_EXIT();
//   return r;
// }

antlrcpp::Any TypeCheckVisitor::visitLeft_expr(AslParser::Left_exprContext *ctx) {
  DEBUG_ENTER();
	if(ctx->ident()) {
		visit(ctx->ident());
	  TypesMgr::TypeId t1 = getTypeDecor(ctx->ident());
	  putTypeDecor(ctx, t1);
		bool b = getIsLValueDecor(ctx->ident());
		putIsLValueDecor(ctx, b);
	}
  else {
		visit(ctx->array_element());
		TypesMgr::TypeId t2 = getTypeDecor(ctx->array_element());
		putTypeDecor(ctx, t2);
		bool b = getIsLValueDecor(ctx->array_element());
		putIsLValueDecor(ctx, b);
	}
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitArithmeticBinary(AslParser::ArithmeticBinaryContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->expr(0));
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  visit(ctx->expr(1));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));

	bool floatArithmetic = Types.isFloatTy(t1) and Types.isFloatTy(t2);
	bool mixArithmetic = (Types.isIntegerTy(t1) and Types.isFloatTy(t2)) or
		(Types.isFloatTy(t1) and Types.isIntegerTy(t2));

  if (((not Types.isErrorTy(t1)) and (not Types.isNumericTy(t1))) or
      ((not Types.isErrorTy(t2)) and (not Types.isNumericTy(t2)))) {
				Errors.incompatibleOperator(ctx->op);
			}

	else if ((ctx->op->getText() == "%") and
			(((not Types.isErrorTy(t1)) and (not Types.isIntegerTy(t1))) or
      ((not Types.isErrorTy(t2)) and (not Types.isIntegerTy(t2))))) {
		std::cout << Types.to_string(1) << "---" << Types.to_string(t2) << std::endl;
    Errors.incompatibleOperator(ctx->op);
	}
	TypesMgr::TypeId t; //	solucionar esto
	if ((floatArithmetic or mixArithmetic) and (ctx->op->getText() == "%")) {
		t = Types.createIntegerTy();
	}	else if (floatArithmetic or mixArithmetic)  {
		t = Types.createFloatTy();
	}	else {
		t = Types.createIntegerTy();
	}
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitRelational(AslParser::RelationalContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->expr(0));
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  visit(ctx->expr(1));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  std::string oper = ctx->op->getText();
  if ((not Types.isErrorTy(t1)) and (not Types.isErrorTy(t2)) and
      (not Types.comparableTypes(t1, t2, oper)))
    Errors.incompatibleOperator(ctx->op);
  TypesMgr::TypeId t = Types.createBooleanTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitValue(AslParser::ValueContext *ctx) {
  DEBUG_ENTER();
  TypesMgr::TypeId t;
	if (ctx->INTVAL()) {
		t = Types.createIntegerTy();
  	putTypeDecor(ctx, t);
	} else if (ctx->FLOATVAL()) {
		t = Types.createFloatTy();
  	putTypeDecor(ctx, t);
	} else if (ctx->CHARVAL()) {
		t = Types.createCharacterTy();
  	putTypeDecor(ctx, t);
	} else if (ctx->BOOLVAL()) {
		t = Types.createBooleanTy();
  	putTypeDecor(ctx, t);
	}
  putIsLValueDecor(ctx, false);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitArrayValue(AslParser::ArrayValueContext *ctx) {
	DEBUG_ENTER();
	visit(ctx->array_element());
	TypesMgr::TypeId elemTy = getTypeDecor(ctx->array_element());
	putTypeDecor(ctx, elemTy);
	bool b = getIsLValueDecor(ctx->array_element());
	putIsLValueDecor(ctx, b);
	DEBUG_EXIT();
  return 0;

}

antlrcpp::Any TypeCheckVisitor::visitProcCallInExpr(AslParser::ProcCallInExprContext *ctx) {
	DEBUG_ENTER();
  visit(ctx->ident());
	TypesMgr::TypeId identTy = getTypeDecor(ctx->ident());
	// check ident es una función
	if (not Types.isFunctionTy(identTy) and not Types.isErrorTy(identTy)) {
    Errors.isNotCallable(ctx->ident());
  }
	TypesMgr::TypeId errorTy = Types.createErrorTy();

	int numParams = 0;
	if (Types.isFunctionTy(identTy))
		numParams = Types.getNumOfParameters(identTy);
	int i = 0;
	// check los parametros tienen el tipo adecuado
	while (ctx->expr(i)) {
		visit(ctx->expr(i));
		if (i < numParams) {
			TypesMgr::TypeId tExpr = getTypeDecor(ctx->expr(i));
			TypesMgr::TypeId tParam = Types.getParameterType(identTy, i);
			if ((not Types.isErrorTy(tExpr)) and (not Types.copyableTypes(tParam, tExpr)))
				Errors.incompatibleParameter(ctx->expr(i), i+1, ctx);
		}
		++i;
	}
	if (Types.isFunctionTy(identTy)) {
		// check numero de parametros correcto
		if (numParams != i)
			Errors.numberOfParameters(ctx->ident());

		// check no es void (y añade el tipo del return)
		TypesMgr::TypeId returnTy = Types.getFuncReturnType(identTy);
		if (Types.isVoidTy(returnTy)) {
			Errors.isNotFunction(ctx->ident());
	    putTypeDecor(ctx, errorTy);
		}
		else {
			putTypeDecor(ctx, returnTy);
		}
	}
	else {
    putTypeDecor(ctx, errorTy);
	}
  putIsLValueDecor(ctx, false);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitExprIdent(AslParser::ExprIdentContext *ctx) {
  DEBUG_ENTER();
  visit(ctx->ident());
  TypesMgr::TypeId t1 = getTypeDecor(ctx->ident());
  putTypeDecor(ctx, t1);
  bool b = getIsLValueDecor(ctx->ident());
  putIsLValueDecor(ctx, b);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitIdent(AslParser::IdentContext *ctx) {
  DEBUG_ENTER();
  std::string ident = ctx->getText();
  if (Symbols.findInStack(ident) == -1) {
    Errors.undeclaredIdent(ctx->ID());
    TypesMgr::TypeId te = Types.createErrorTy();
    putTypeDecor(ctx, te);
    putIsLValueDecor(ctx, true);
  }
  else {
    TypesMgr::TypeId t1 = Symbols.getType(ident);
    putTypeDecor(ctx, t1);
    if (Symbols.isFunctionClass(ident))
      putIsLValueDecor(ctx, false);
    else
      putIsLValueDecor(ctx, true);
  }
  DEBUG_EXIT();
  return 0;
}


antlrcpp::Any TypeCheckVisitor::visitParenthesisExpr(AslParser::ParenthesisExprContext *ctx) {
	DEBUG_ENTER();
	visit(ctx->expr());
	TypesMgr::TypeId t = getTypeDecor(ctx->expr());
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
	DEBUG_EXIT();
	return 0;
}

antlrcpp::Any TypeCheckVisitor::visitArithmeticUnary(AslParser::ArithmeticUnaryContext *ctx) {
	DEBUG_ENTER();
	visit(ctx->expr());
	TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
	std::string oper = ctx->op->getText();
	if ((not Types.isErrorTy(t1)) and (not Types.isNumericTy(t1)))
		Errors.incompatibleOperator(ctx->op);

	TypesMgr::TypeId t = Types.createIntegerTy();
	if (Types.isFloatTy(t1))
		putTypeDecor(ctx, t1);
	else
		putTypeDecor(ctx, t);

	putIsLValueDecor(ctx, false);
	DEBUG_EXIT();
	return 0;
}

antlrcpp::Any TypeCheckVisitor::visitBooleanUnary(AslParser::BooleanUnaryContext *ctx) {
	DEBUG_ENTER();
	visit(ctx->expr());
	TypesMgr::TypeId t1 = getTypeDecor(ctx->expr());
	std::string oper = ctx->op->getText();
	if ((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1)))
		Errors.incompatibleOperator(ctx->op);
	TypesMgr::TypeId t = Types.createBooleanTy();
	putTypeDecor(ctx, t);
	putIsLValueDecor(ctx, false);
	DEBUG_EXIT();
	return 0;
}

antlrcpp::Any TypeCheckVisitor::visitBooleanBinary(AslParser::BooleanBinaryContext *ctx) {
	DEBUG_ENTER();
  visit(ctx->expr(0));
  TypesMgr::TypeId t1 = getTypeDecor(ctx->expr(0));
  visit(ctx->expr(1));
  TypesMgr::TypeId t2 = getTypeDecor(ctx->expr(1));
  std::string oper = ctx->op->getText();
	if (((not Types.isErrorTy(t1)) and (not Types.isBooleanTy(t1))) or
      ((not Types.isErrorTy(t2)) and (not Types.isBooleanTy(t2))))
    Errors.incompatibleOperator(ctx->op);
  TypesMgr::TypeId t = Types.createBooleanTy();
  putTypeDecor(ctx, t);
  putIsLValueDecor(ctx, false);
  DEBUG_EXIT();
  return 0;
}

antlrcpp::Any TypeCheckVisitor::visitArray_element(AslParser::Array_elementContext *ctx) {
	DEBUG_ENTER();
	visit(ctx->ident());
	TypesMgr::TypeId identTy = getTypeDecor(ctx->ident());
	if ((not Types.isErrorTy(identTy)) and (not Types.isArrayTy(identTy)))
		Errors.nonArrayInArrayAccess(ctx);
	visit(ctx->expr());
	TypesMgr::TypeId exprTy = getTypeDecor(ctx->expr());
	if ((not Types.isErrorTy(exprTy)) and (not Types.isIntegerTy(exprTy)))
		Errors.nonIntegerIndexInArrayAccess(ctx->expr());

	if (Types.isArrayTy(identTy)) {
		putTypeDecor(ctx, Types.getArrayElemType(identTy));
		putIsLValueDecor(ctx, true);
	} else {
		putTypeDecor(ctx, Types.createErrorTy());
		putIsLValueDecor(ctx, false);
	}
	DEBUG_EXIT();
	return 0;
}




// Getters for the necessary tree node atributes:
//   Scope, Type ans IsLValue
SymTable::ScopeId TypeCheckVisitor::getScopeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getScope(ctx);
}
TypesMgr::TypeId TypeCheckVisitor::getTypeDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getType(ctx);
}
bool TypeCheckVisitor::getIsLValueDecor(antlr4::ParserRuleContext *ctx) {
  return Decorations.getIsLValue(ctx);
}

// Setters for the necessary tree node attributes:
//   Scope, Type ans IsLValue
void TypeCheckVisitor::putScopeDecor(antlr4::ParserRuleContext *ctx, SymTable::ScopeId s) {
  Decorations.putScope(ctx, s);
}
void TypeCheckVisitor::putTypeDecor(antlr4::ParserRuleContext *ctx, TypesMgr::TypeId t) {
  Decorations.putType(ctx, t);
}
void TypeCheckVisitor::putIsLValueDecor(antlr4::ParserRuleContext *ctx, bool b) {
  Decorations.putIsLValue(ctx, b);
}
