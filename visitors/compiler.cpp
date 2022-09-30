#include "compiler.hpp"

#include "../ir/token.hpp"
#include "../ir/expressions.hpp"
#include "../ir/statements.hpp"

std::map<TokenTypes, std::pair<int, bool>> typeMap = {
    {I8, {8, true}},
    {I16, {16, true}},
    {I32, {32, true}},
    {I64, {64, true}},
    {U8, {8, false}},
    {U16, {16, false}},
    {U32, {32, false}},
    {U64, {64, false}},
};

llvm::Value *Compiler::visit(IntExpression &expression)
{

    return llvm::ConstantInt::get(*context, llvm::APInt(64, expression.value, true));
}

llvm::Value *Compiler::visit(VarExpression &statement)
{
    return env->get(statement.name);
}

llvm::Value *Compiler::visit(BinaryExpression &expression)
{
    llvm::Value *left = expression.left->accept(*this);
    llvm::Value *right = expression.right->accept(*this);

    switch (expression.op.type)
    {
    case PLUS:
        return builder->CreateAdd(left, right, "addtmp");
    case MINUS:
        return builder->CreateSub(left, right, "subtmp");
    case STAR:
        return builder->CreateMul(left, right, "multmp");
    case SLASH:
        return builder->CreateSDiv(left, right, "divtmp");
    case EQUAL_EQUAL:
        return builder->CreateICmpEQ(left, right, "eqtmp");
    case NOT_EQUAL:
        return builder->CreateICmpNE(left, right, "netmp");
    case LESS:
        return builder->CreateICmpSLT(left, right, "lttmp");
    case LESS_EQUAL:
        return builder->CreateICmpSLE(left, right, "letmp");
    case GREATER:
        return builder->CreateICmpSGT(left, right, "gttmp");
    case GREATER_EQUAL:
        return builder->CreateICmpSGE(left, right, "getmp");
    default:
        return nullptr;
    }
}

llvm::Value *Compiler::visit(ExpressionStatement &statement)
{

    auto value = statement.expression->accept(*this);
    return value;
}

llvm::Value *Compiler::visit(PrintStatement &statement)
{

    llvm::Function *printFunc = module->getFunction("printf");
    std::vector<llvm::Value *> args;
    auto value = statement.expression->accept(*this);
    args.push_back(builder->CreateGlobalStringPtr("%d\n"));
    args.push_back(value);
    builder->CreateCall(printFunc, args, "printf");

    return nullptr;
}

llvm::Value *Compiler::visit(LetStatement &statement)
{
    int bitWidth;
    bool isSigned;

    std::tie(bitWidth, isSigned) = typeMap[statement.type];

    llvm::Value *value = statement.expression->accept(*this);
    llvm::Value *newValue = builder->CreateIntCast(value, llvm::Type::getIntNTy(*context, bitWidth), isSigned);
    env->define(statement.name, newValue);

    return newValue;
}

llvm::Value *Compiler::visit(BlockStatement &statement)
{
    // llvm::BasicBlock *innerBlock = llvm::BasicBlock::Create(*context, "innerBlock", mainFunction);
    // builder->SetInsertPoint(innerBlock);

    // TODO: stop using new for envs
    Environment *newEnv = new Environment(env);
    env = newEnv;

    for (auto &stmt : statement.statements)
    {
        stmt->accept(*this);
    }

    env = env->enclosing;

    return nullptr;
}

llvm::Value *Compiler::visit(IfStatement &statement)
{
    llvm::Value *condition = statement.condition->accept(*this);

    llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*context, "then", mainFunction);
    llvm::BasicBlock *elseBlock = llvm::BasicBlock::Create(*context, "else");
    llvm::BasicBlock *mergeBlock = llvm::BasicBlock::Create(*context, "ifcont");

    builder->CreateCondBr(condition, thenBlock, elseBlock);

    builder->SetInsertPoint(thenBlock);
    statement.thenBranch->accept(*this);
    builder->CreateBr(mergeBlock);

    thenBlock = builder->GetInsertBlock();

    if (statement.elseBranch)
    {

        mainFunction->getBasicBlockList().push_back(elseBlock);
        builder->SetInsertPoint(elseBlock);

        statement.elseBranch->accept(*this);

        builder->CreateBr(mergeBlock);

        elseBlock = builder->GetInsertBlock();
    }

    mainFunction->getBasicBlockList().push_back(mergeBlock);
    builder->SetInsertPoint(mergeBlock);

    return nullptr;
}

int Compiler::compile(std::vector<std::unique_ptr<Statement>> &statements)
{

    // create an external function to to printf that only takes one number (long long)
    std::vector<llvm::Type *> printfArgs;
    printfArgs.push_back(llvm::Type::getInt8PtrTy(*context));
    llvm::FunctionType *printfType = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), printfArgs, true);
    llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, llvm::Twine("printf"), module.get());

    mainFunction = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), false),
        llvm::Function::ExternalLinkage,
        "main",
        module.get());
    llvm::BasicBlock *block = llvm::BasicBlock::Create(*context, "entry", mainFunction);
    builder->SetInsertPoint(block);

    for (auto &statement : statements)
    {
        auto x = statement->accept(*this);
    }

    // return statement
    builder->CreateRetVoid();

    module->print(llvm::outs(), nullptr);

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    std::string Error;
    auto Target = llvm::TargetRegistry::lookupTarget(targetTriple, Error);

    if (!Target)
    {
        llvm::errs() << Error;
        return 1;
    }

    auto CPU = "generic";
    auto Features = "";

    llvm::TargetOptions opt;
    auto RM = llvm::Optional<llvm::Reloc::Model>();
    auto targetMachine = Target->createTargetMachine(targetTriple, CPU, Features, opt, RM);

    module->setDataLayout(targetMachine->createDataLayout());

    auto fileName = "output.o";
    std::error_code EC;
    llvm::raw_fd_ostream dest(fileName, EC, llvm::sys::fs::OF_None);

    if (EC)
    {
        llvm::errs() << "Could not open file: " << EC.message();
        return 1;
    }

    llvm::legacy::PassManager pass;
    auto FileType = llvm::CGFT_ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType))
    {
        llvm::errs() << "TheTargetMachine can't emit a file of this type";
        return 1;
    }

    pass.run(*module);
    dest.flush();




    return 0;
}
