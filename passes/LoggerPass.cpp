#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace {

static bool isSkippableFunction(const Function &F) {
	if (F.isDeclaration()) return true;
	if (F.isIntrinsic()) return true;
	StringRef Name = F.getName();
	if (Name.empty()) return false;
	if (Name.equals("printf")) return true;
	if (Name.starts_with("__logger")) return true;
	return false;
}

static FunctionCallee getOrInsertPrintf(Module &M) {
	LLVMContext &Ctx = M.getContext();
	FunctionType *PrintfTy = FunctionType::get(IntegerType::getInt32Ty(Ctx),
	                                          PointerType::getUnqual(Type::getInt8Ty(Ctx)), true);
	return M.getOrInsertFunction("printf", PrintfTy);
}

static Value *getFunctionNameGlobal(IRBuilder<> &B, Module &M, Function &F) {
	return B.CreateGlobalStringPtr(F.getName(), Twine("__logger.fn.") + F.getName());
}

static void emitPrintfEnter(IRBuilder<> &B, Module &M, Value *FnNameStr) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr(">> %s\n", "__logger.fmt.enter");
	B.CreateCall(Printf, {Fmt, FnNameStr});
}

static void emitPrintfAggregate(IRBuilder<> &B, Module &M, Value *FnNameStr, int ArgIndex) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("   %s(arg%d)=(aggregate)\n", "__logger.fmt.arg.agg");
	Value *Idx = B.getInt32(ArgIndex);
	B.CreateCall(Printf, {Fmt, FnNameStr, Idx});
}

static void emitPrintfArgInt(IRBuilder<> &B, Module &M, Value *FnNameStr, int ArgIndex, Value *V) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("   %s(arg%d)=%lld\n", "__logger.fmt.arg.i");
	Value *Idx = B.getInt32(ArgIndex);
	// Zero-extend to i64 for uniform printing
	Value *I64 = B.CreateZExtOrBitCast(V, B.getInt64Ty());
	B.CreateCall(Printf, {Fmt, FnNameStr, Idx, I64});
}

static void emitPrintfArgFloat(IRBuilder<> &B, Module &M, Value *FnNameStr, int ArgIndex, Value *V) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("   %s(arg%d)=%f\n", "__logger.fmt.arg.f");
	Value *Idx = B.getInt32(ArgIndex);
	// Cast to double for printf
	Value *D = V->getType()->isDoubleTy() ? V : B.CreateFPExt(V, B.getDoubleTy());
	B.CreateCall(Printf, {Fmt, FnNameStr, Idx, D});
}

static void emitPrintfArgPtr(IRBuilder<> &B, Module &M, Value *FnNameStr, int ArgIndex, Value *V) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("   %s(arg%d)=%p\n", "__logger.fmt.arg.p");
	Value *Idx = B.getInt32(ArgIndex);
	Value *P = B.CreatePointerCast(V, PointerType::getUnqual(Type::getInt8Ty(M.getContext())));
	B.CreateCall(Printf, {Fmt, FnNameStr, Idx, P});
}

static void emitPrintfRetVoid(IRBuilder<> &B, Module &M, Value *FnNameStr) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("<< %s returns void\n", "__logger.fmt.ret.v");
	B.CreateCall(Printf, {Fmt, FnNameStr});
}

static void emitPrintfRetInt(IRBuilder<> &B, Module &M, Value *FnNameStr, Value *V) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("<< %s returns %lld\n", "__logger.fmt.ret.i");
	Value *I64 = B.CreateZExtOrBitCast(V, B.getInt64Ty());
	B.CreateCall(Printf, {Fmt, FnNameStr, I64});
}

static void emitPrintfRetFloat(IRBuilder<> &B, Module &M, Value *FnNameStr, Value *V) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("<< %s returns %f\n", "__logger.fmt.ret.f");
	Value *D = V->getType()->isDoubleTy() ? V : B.CreateFPExt(V, B.getDoubleTy());
	B.CreateCall(Printf, {Fmt, FnNameStr, D});
}

static void emitPrintfRetPtr(IRBuilder<> &B, Module &M, Value *FnNameStr, Value *V) {
	FunctionCallee Printf = getOrInsertPrintf(M);
	Value *Fmt = B.CreateGlobalStringPtr("<< %s returns %p\n", "__logger.fmt.ret.p");
	Value *P = B.CreatePointerCast(V, PointerType::getUnqual(Type::getInt8Ty(M.getContext())));
	B.CreateCall(Printf, {Fmt, FnNameStr, P});
}

class LoggerFunctionPass : public PassInfoMixin<LoggerFunctionPass> {
public:
	PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
		if (isSkippableFunction(F)) {
			return PreservedAnalyses::all();
		}

		Module &M = *F.getParent();
		BasicBlock &Entry = F.getEntryBlock();
		IRBuilder<> B(&*Entry.getFirstInsertionPt());
		Value *FnNameStr = getFunctionNameGlobal(B, M, F);
		emitPrintfEnter(B, M, FnNameStr);

		int argIndex = 0;
		for (Argument &Arg : F.args()) {
			Type *T = Arg.getType();
			IRBuilder<> BA(&*Entry.getFirstInsertionPt());
			if (T->isPointerTy()) {
				emitPrintfArgPtr(BA, M, FnNameStr, argIndex, &Arg);
			} else if (T->isIntegerTy()) {
				emitPrintfArgInt(BA, M, FnNameStr, argIndex, &Arg);
			} else if (T->isFloatingPointTy()) {
				emitPrintfArgFloat(BA, M, FnNameStr, argIndex, &Arg);
			} else {
				emitPrintfAggregate(BA, M, FnNameStr, argIndex);
			}
			argIndex++;
		}

		SmallVector<ReturnInst *, 8> Returns;
		for (BasicBlock &BB : F) {
			if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
				Returns.push_back(RI);
			}
		}

		for (ReturnInst *RI : Returns) {
			IRBuilder<> BR(RI);
			if (RI->getNumOperands() == 0) {
				emitPrintfRetVoid(BR, M, FnNameStr);
				continue;
			}
			Value *RetVal = RI->getReturnValue();
			Type *RT = RetVal->getType();
			if (RT->isPointerTy()) {
				emitPrintfRetPtr(BR, M, FnNameStr, RetVal);
			} else if (RT->isIntegerTy()) {
				emitPrintfRetInt(BR, M, FnNameStr, RetVal);
			} else if (RT->isFloatingPointTy()) {
				emitPrintfRetFloat(BR, M, FnNameStr, RetVal);
			} else {
				FunctionCallee Printf = getOrInsertPrintf(M);
				Value *Fmt = BR.CreateGlobalStringPtr("<< %s returns (aggregate)\n", "__logger.fmt.ret.agg");
				BR.CreateCall(Printf, {Fmt, FnNameStr});
			}
		}

		return PreservedAnalyses::none();
	}
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
	return {
		LLVM_PLUGIN_API_VERSION,
		"LoggerPass",
		LLVM_VERSION_STRING,
		[](PassBuilder &PB) {
			PB.registerPipelineStartEPCallback(
				[](ModulePassManager &MPM, OptimizationLevel) {
					MPM.addPass(createModuleToFunctionPassAdaptor(LoggerFunctionPass()));
				});
			PB.registerOptimizerLastEPCallback(
				[](ModulePassManager &MPM, OptimizationLevel) {
					MPM.addPass(createModuleToFunctionPassAdaptor(LoggerFunctionPass()));
				});
			PB.registerPipelineParsingCallback(
				[](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
					if (Name == "logger-fn") {
						FPM.addPass(LoggerFunctionPass());
						return true;
					}
					return false;
				});
		}
	};
}


