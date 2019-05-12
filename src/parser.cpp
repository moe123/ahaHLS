#include "parser.h"

#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar.h"
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/LoopAccessAnalysis.h>
#include <llvm/Analysis/ScalarEvolution.h>

#include <functional>

#include "verilog_backend.h"
#include "test_utils.h"

using namespace dbhc;
using namespace std;

namespace ahaHLS {

  maybe<Token> parseStr(const std::string target, TokenState& chars) {
    string str = "";
    for (int i = 0; i < (int) target.size(); i++) {
      if (chars.atEnd()) {
        return maybe<Token>();
      }

      char next = chars.parseChar();
      if (target[i] == next) {
        str += next;
      } else {
        return maybe<Token>();
      }
    }
  
    return maybe<Token>(Token(str));
  }

  std::function<maybe<Token>(TokenState& chars)> mkParseStr(const std::string str) {
    return [str](TokenState& state) { return parseStr(str, state); };
  }

  maybe<Token> parseComment(TokenState& state) {
    // Parse any number of comment lines and whitespace
    auto result = tryParse<Token>(mkParseStr("//"), state);
    if (result.has_value()) {
      while (!state.atEnd() && !(state.peekChar() == '\n')) {
        state.parseChar();
      }

      if (!state.atEnd()) {
        cout << "Parsing end char" << endl;
        state.parseChar();
      }

      return Token("//");

    } else {
      return maybe<Token>();
    }

  }

  maybe<FunctionCall*> parseFunctionCall(ParseState<Token>& tokens) {
    Token t = tokens.parseChar();
    if (!t.isId()) {
      return maybe<FunctionCall*>();
    }

    Token paren = tokens.parseChar();
    if (paren != Token("(")) {
      return maybe<FunctionCall*>();
    }

    //cout << "parsing funcall " << tokens.remainder() << endl;
    vector<Expression*> callArgs =
      sepBtwn0<Expression*, Token>(parseExpressionMaybe, parseComma, tokens);

    paren = tokens.parseChar();
    if (paren != Token(")")) {
      return maybe<FunctionCall*>();
    }

    return new FunctionCall(t, callArgs);
  }

  maybe<Expression*> parseExpressionMaybe(ParseState<Token>& tokens) {
    //cout << "-- Parsing expression " << tokens.remainder() << endl;

    vector<Token> operatorStack;
    vector<Expression*> postfixString;
  
    while (true) {
      auto pExpr = parsePrimitiveExpressionMaybe(tokens);
      //cout << "After primitive expr = " << tokens.remainder() << endl;
      if (!pExpr.has_value()) {
        break;
      }

      postfixString.push_back(pExpr.get_value());
    
      if (tokens.atEnd() || !isBinop(tokens.peekChar())) {
        break;
      }

      Token binop = tokens.parseChar();
      if (!isBinop(binop)) {
        break;
      }

      //cout << "Adding binop " << binop << endl;
      if (operatorStack.size() == 0) {
        //cout << tab(1) << "Op stack empty " << binop << endl;      
        operatorStack.push_back(binop);
      } else if (precedence(binop) > precedence(operatorStack.back())) {
        //cout << tab(1) << "Op has higher precedence " << binop << endl;      
        operatorStack.push_back(binop);
      } else {
        while (true) {
          Token topOp = operatorStack.back();
          operatorStack.pop_back();

          //cout << "Popping " << topOp << " from op stack" << endl;

          postfixString.push_back(new Identifier(topOp));
        
          if ((operatorStack.size() == 0) ||
              (precedence(binop) > precedence(operatorStack.back()))) {
            break;
          }
        }

        operatorStack.push_back(binop);
      }
    }

    // Pop and print all operators on the stack
    //cout << "Adding ops" << endl;
    // Reverse order of this?
    for (auto op : operatorStack) {
      //cout << tab(1) << "Popping operator " << op << endl;
      postfixString.push_back(new Identifier(op));
    }

    if (postfixString.size() == 0) {
      return maybe<Expression*>();
    }

    //cout << "Building final value" << endl;
    //cout << "Postfix string" << endl;
    // for (auto s : postfixString) {
    //   cout << tab(1) << *s << endl;
    // }

    Expression* final = popOperand(postfixString);
    assert(postfixString.size() == 0);
    assert(final != nullptr);

    //cout << "Returning expression " << *final << endl;
    return final;
  }

  Expression* parseExpression(ParseState<Token>& tokens) {
    auto res = parseExpressionMaybe(tokens);
    if (res.has_value()) {
      return res.get_value();
    }

    assert(false);
  }

  maybe<Statement*> parsePipeline(ParseState<Token>& tokens) {
    if (!tokens.nextCharIs(Token("pipeline"))) {
      return maybe<Statement*>();
    }

    auto iiExpr = parseExpressionMaybe(tokens);
    if (!iiExpr.has_value()) {
      return maybe<Statement*>();
    }

    if (!tokens.nextCharIs(Token("{"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    auto stmts = many<Statement*>(parseStatement, tokens);

    if (!tokens.nextCharIs(Token("}"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();
    

    return new PipelineBlock(iiExpr.get_value(), stmts);
  }
  
  maybe<Statement*> parseForLoop(ParseState<Token>& tokens) {

    //cout << "Parsing for loop " << tokens.remainder() << endl;

    if (!tokens.nextCharIs(Token("for"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    //cout << "for loop decl " << tokens.remainder() << endl;  

    if (!tokens.nextCharIs(Token("("))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    //cout << "Getting for init " << tokens.remainder() << endl;
  
    auto init = parseStatement(tokens);
    if (!init.has_value()) {
      return maybe<Statement*>();
    }

    //cout << "Getting for test " << tokens.remainder() << endl;

    auto test = parseExpressionMaybe(tokens);
    if (!test.has_value()) {
      return maybe<Statement*>();
    }
  
    if (!tokens.nextCharIs(Token(";"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    auto update = parseStatement(tokens);
    if (!update.has_value()) {
      return maybe<Statement*>();
    }
  
    if (!tokens.nextCharIs(Token(")"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    if (!tokens.nextCharIs(Token("{"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    auto stmts = many<Statement*>(parseStatement, tokens);

    if (!tokens.nextCharIs(Token("}"))) {
      return maybe<Statement*>();
    }
    tokens.parseChar();

    return new ForStmt(init.get_value(), test.get_value(), update.get_value(), stmts);
  
  }

  maybe<Statement*> parseStatementNoLabel(ParseState<Token>& tokens) {
    // Try to parse for loop
    auto doStmt = tryParse<Statement*>(parseDoWhileLoop, tokens);
    if (doStmt.has_value()) {
      return doStmt;
    }

    auto forStmt = tryParse<Statement*>(parseForLoop, tokens);
    if (forStmt.has_value()) {
      return forStmt;
    }

    auto pipeStmt = tryParse<Statement*>(parsePipeline, tokens);
    if (pipeStmt.has_value()) {
      return pipeStmt;
    }
    
    if (tokens.peekChar() == Token("return")) {
      tokens.parseChar();
      auto e = parseExpressionMaybe(tokens);
      if (e.has_value()) {
        if (tokens.nextCharIs(Token(";"))) {
          tokens.parseChar();
          return new ReturnStmt(e.get_value());
        } else {
          return maybe<Statement*>();
        }
      } else {

        if (tokens.nextCharIs(Token(";"))) {
          tokens.parseChar();
          return new ReturnStmt();
        } else {
          return maybe<Statement*>();
        }

      }
    }
  
    if (tokens.peekChar() == Token("class")) {
      tokens.parseChar();
      Token name = tokens.parseChar();

      assert(tokens.parseChar() == Token("{"));

      vector<Statement*> classStmts =
        many<Statement*>(parseStatement, tokens);

      assert(tokens.parseChar() == Token("}"));
      assert(tokens.parseChar() == Token(";"));

      return new ClassDecl(name, classStmts);
    }

    maybe<Statement*> funcDecl =
      tryParse<Statement*>(parseFuncDecl, tokens);

    if (funcDecl.has_value()) {
      return funcDecl;
    }

    //cout << "Statement after trying funcDecl " << tokens.remainder() << endl;  

    auto assign = tryParse<Statement*>(parseAssignStmt, tokens);
    if (assign.has_value()) {
      if (tokens.nextCharIs(Token(";"))) {
        tokens.parseChar();
      }
      return assign;
    }

    //cout << "Statement after assign " << tokens.remainder() << endl;

    // Should do: tryParse function declaration
    // Then: tryParse member declaration
    int posBefore = tokens.currentPos();
    auto decl = tryParse<ArgumentDecl*>(parseArgDeclMaybe, tokens);
  
    if (decl.has_value()) {
      if (tokens.peekChar() == Token(";")) {
        tokens.parseChar();
        return decl.get_value();
      }
    }

    tokens.setPos(posBefore);

    auto call = tryParse<Expression*>(parseExpressionMaybe, tokens);

    if (call.has_value() && tokens.nextCharIs(Token(";"))) {
      tokens.parseChar();
      return new ExpressionStmt(call.get_value());
    }


    return maybe<Statement*>();
  }

  maybe<Statement*> parseStatement(ParseState<Token>& tokens) {
    //cout << "Starting to parse statement " << tokens.remainder() << endl;
  
    if (tokens.atEnd()) {
      return maybe<Statement*>();
    }
  
    // Try to parse a label?
    auto label = tryParse<Token>(parseLabel, tokens);
  
    auto stmt = parseStatementNoLabel(tokens);

    if (!stmt.has_value()) {
      return maybe<Statement*>();
    }

    if (label.has_value()) {
      auto stmtV = stmt.get_value();
      stmtV->setLabel(label.get_value());
      return stmtV;
    }
    return stmt;
  }

  void optimizeModuleLLVM(SynthCppModule& mod);
  void optimizeModuleLLVM(llvm::Module& mod);

  void setZeroRows(TestBenchSpec& tb, const int cycleNo, const int stencilWidth, const int loadWidth, vector<Argument*> words) {
    for (int row = 0; row < stencilWidth; row++) {
      auto word = words[row];
      string values = "";
      for (int col = 0; col < loadWidth; col++) {
        values += "8'd0";
        if (col != (loadWidth - 1)) {
          values += ",";
        }
      }
      tb.setArgPort(word, "in_wire", cycleNo,
                    "{" + values + "}");
    }
  }


  // Simpler store elimination, since our formulation has no aliasing by
  // construction
  void optimizeStores(llvm::Function* f) {
    bool erased = true;
    while (erased) {
      erased = false;
      for (auto& bb : f->getBasicBlockList()) {
        for (auto& instr : bb) {
          //int numUses = instr.getNumUses();

          if (AllocaInst::classof(&instr)) {
            bool allStores = true;
            for (auto& user : instr.uses()) {
              if (!StoreInst::classof(user.getUser())) {
                allStores = false;
              }
            }

            if (allStores) {

              cout << "All uses of " << valueString(&instr) << " are stores" << endl;
              for (auto& user : instr.uses()) {
                cout << "Erasing " << valueString(user) << endl;
                dyn_cast<Instruction>(user.getUser())->eraseFromParent();
              }

              instr.eraseFromParent();
            
              erased = true;
              break;
            }
          }
        }

        if (erased) {
          break;
        }
      }
    }
  }

  MemorySpec pss(const int width) {
    return {0, 0, 1, 1, width, 1, false, {{{"width", std::to_string(width)}}, "reg_passthrough"}};
  }

  bool isPresent(InstructionTime& time, llvm::Function* const f) {
    ExecutionAction action = time.action;
    if (action.isTag()) {
      return true;
    }

    if (action.isBasicBlock()) {
      BasicBlock* bb = action.getBasicBlock();
      for (auto& other : f->getBasicBlockList()) {
        if (bb == &other) {
          return true;
        }
      }

      return false;
    }

    if (action.isInstruction()) {
      Instruction* instr = action.getInstruction();
      for (auto& bb : f->getBasicBlockList()) {
        for (auto& other : bb) {
          if (instr == &other) {
            return true;
          }
        }
      }

      return false;
    }

    return false;
  }

  void clearExecutionConstraints(llvm::Function* const f,
                                 ExecutionConstraints& exec) {
    set<ExecutionConstraint*> toRemove;
    for (auto c : exec.constraints) {
      if (c->type() == CONSTRAINT_TYPE_ORDERED) {
        auto oc = static_cast<Ordered*>(c);
        bool startPresent = isPresent(oc->before, f);
        bool endPresent = isPresent(oc->after, f);

        if (!startPresent || !endPresent) {
          toRemove.insert(c);
        }
      } else {
        assert(false);
      }
    }

    for (auto r : toRemove) {
      exec.remove(r);
    }
  }

  Schedule scheduleInterfaceZeroReg(llvm::Function* f,
                                    HardwareConstraints& hcs,
                                    InterfaceFunctions& interfaces,
                                    std::set<BasicBlock*>& toPipeline,
                                    ExecutionConstraints& exec) {

    cout << "Before inlining" << endl;
    cout << valueString(f) << endl;

    addDataConstraints(f, exec);
    inlineWireCalls(f, exec, interfaces);

    // TODO: Where to put this stuff
    optimizeModuleLLVM(*(f->getParent()));
    optimizeStores(f);
    clearExecutionConstraints(f, exec);
  
    cout << "After inlining" << endl;
    cout << valueString(f) << endl;

    setAllAllocaMemTypes(hcs, f, pss(32));
    hcs.memoryMapping =
      memoryOpLocations(f);

    // Single load optimization
    for (auto& bb : f->getBasicBlockList()) {
      for (auto& instrR : bb) {
        auto instr = &instrR;
        int numUsers = instr->getNumUses();

        if ((BinaryOperator::classof(instr) ||
             UnaryInstruction::classof(instr) ||
             LoadInst::classof(instr) ||
             CmpInst::classof(instr))
            && (numUsers == 1)) {
          auto& user = *(instr->uses().begin());
          assert(Instruction::classof(user));
          auto userInstr = dyn_cast<Instruction>(user.getUser());
          exec.add(instrEnd(instr) == instrStart(userInstr));
        }
      }
    }

    // cout << "Hardware memory storage names" << endl;
    // for (auto mspec : hcs.memSpecs) {
    //   cout << valueString(mspec.first) << " -> " << mspec.second.modSpec.name << endl;
    // }

    SchedulingProblem p = createSchedulingProblem(f, hcs, toPipeline);
    exec.addConstraints(p, f);

    map<Function*, SchedulingProblem> constraints{{f, p}};
    Schedule s = scheduleFunction(f, hcs, toPipeline, constraints);

    return s;
  }

  pair<string, int> extractDefault(Statement* stmt) {
    auto eStmt = extract<ExpressionStmt>(stmt);
    auto expr = eStmt->expr;
    FunctionCall* writeCall = extract<FunctionCall>(expr);
    assert(writeCall->getName() == "write_port");
    Expression* portId = writeCall->args[0];
    Identifier* id = extract<Identifier>(portId);
    Expression* portVal = writeCall->args[1];
    IntegerExpr* val = extract<IntegerExpr>(portVal);

    return {id->getName(), val->getInt()};
  }

  int countTerminators(const BasicBlock& bb) {
    int numTerminators = 0;
    for (auto& instr : bb) {
      if (TerminatorInst::classof(&instr)) {
        numTerminators++;
      }
    }

    return numTerminators;
  }

  void sanityCheck(llvm::Function* f) {
    for (auto& bb : f->getBasicBlockList()) {
      int numTerminators = countTerminators(bb);
      if (numTerminators != 1) {
        cout << "Error: In function " << endl;
        cout << valueString(f) << endl;
        cout << "basic block " << endl;
        cout << valueString(&bb) << " has zero or multiple terminators" << endl;
        assert(false);
      }
    }
  }

  // Idea: Caller constraints that inline in to each user of a function?
  // For each called user function check if caller constraints are satisified
  // and if not propagate them up the stack? A form of type checking I suppose?
  void optimizeModuleLLVM(llvm::Module& mod) {
    llvm::legacy::PassManager pm;
    pm.add(new LoopInfoWrapperPass());
    pm.add(new AAResultsWrapperPass());
    pm.add(new TargetLibraryInfoWrapperPass());
    pm.add(createGVNPass());
    pm.add(createDeadStoreEliminationPass());
      
    pm.run(mod);
  }

  void optimizeModuleLLVM(SynthCppModule& scppMod) {
    optimizeModuleLLVM(*(scppMod.mod.get()));
  }

  MicroArchitecture
  synthesizeVerilog(SynthCppModule& scppMod, const std::string& funcName) {
    SynthCppFunction* f = scppMod.getFunction(funcName);

    // Q: How do we pass the hardware constraints on f in to the synthesis flow?
    cout << "Scheduling function" << endl;
    cout << valueString(f->llvmFunction()) << endl;

    cout << "In synthesize verilog: # of interface functions = " << scppMod.getInterfaceFunctions().constraints.size() << endl;
    for (auto func : scppMod.getInterfaceFunctions().constraints) {
      cout << tab(1) << "# of constraints on " <<
        string(func.first->getName()) << " = " <<
        func.second.constraints.size() << endl;
    }

    // Set pointers to primitives to be registers, not memories
    for (auto& arg : f->llvmFunction()->args()) {
      Type* argTp = arg.getType();
      if (PointerType::classof(argTp)) {
        Type* underlying = dyn_cast<PointerType>(argTp)->getElementType();
        if (IntegerType::classof(underlying)) {
          cout << "Should set " << valueString(&arg) << " to be register" << endl;
          scppMod.getHardwareConstraints().modSpecs.insert({&arg, registerModSpec(getTypeBitWidth(underlying))});
          scppMod.getHardwareConstraints().memSpecs.insert({&arg, registerSpec(getTypeBitWidth(underlying))});
        }
      }
    }
  
    Schedule s =
      scheduleInterfaceZeroReg(f->llvmFunction(),
                               scppMod.getHardwareConstraints(),
                               scppMod.getInterfaceFunctions(),
                               scppMod.getBlocksToPipeline(),
                               scppMod.getInterfaceFunctions().getConstraints(f->llvmFunction()));

    STG graph = buildSTG(s, f->llvmFunction());

    // TODO: Generate these automatically, or change generation code
    // to treat LLVM i<N> as builtin?

    // cout << "Hardware memory constraints before " << endl;
    // for (auto mspec : scppMod.getHardwareConstraints().memSpecs) {
    //   cout << valueString(mspec.first) << " -> " << mspec.second.modSpec.name << endl;
    // }
  
    //setAllAllocaMemTypes(scppMod.getHardwareConstraints(), f->llvmFunction(), registerSpec(32));

    cout << "STG is" << endl;
    graph.print(cout);

    map<Value*, int> layout;  
    auto arch = buildMicroArchitecture(graph, layout, scppMod.getHardwareConstraints());

    VerilogDebugInfo info;
    emitVerilog(arch, info);

    return arch;
  }
}
