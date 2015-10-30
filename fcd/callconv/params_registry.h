//
// params_registry.h
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is part of fcd.
// 
// fcd is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// fcd is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with fcd.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef register_use_hpp
#define register_use_hpp

#include "llvm_warnings.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/Function.h>
#include <llvm/ADT/SmallVector.h>
SILENCE_LLVM_WARNINGS_END()

#include <cassert>
#include <deque>
#include <string>
#include <unordered_map>

class CallingConvention;
class Executable;
class TargetInfo;
class TargetRegisterInfo;

struct ValueInformation
{
	enum StorageClass
	{
		IntegerRegister,
		FloatingPointRegister,
		Stack,
	};
	
	StorageClass type;
	union
	{
		const char* registerName;
		uint64_t frameBaseOffset;
	};
	
	ValueInformation(StorageClass regType, uint64_t frameBaseOffset)
	: type(regType), frameBaseOffset(frameBaseOffset)
	{
		assert(type == Stack);
	}
	
	ValueInformation(StorageClass regType, const char* registerName)
	: type(regType), registerName(registerName)
	{
		assert(type != Stack);
	}
};

struct CallInformation
{
	const char* callingConvention;
	llvm::SmallVector<ValueInformation, 1> returnValues;
	llvm::SmallVector<ValueInformation, 7> parameters;
	
	CallInformation(const char* callingConvention)
	: callingConvention(callingConvention)
	{
	}
	
	llvm::AliasAnalysis::ModRefResult getRegisterModRef(const TargetRegisterInfo& reg) const;
};

class ParameterRegistry
{
	CallingConvention* defaultCC;
	TargetInfo& target;
	Executable& executable;
	std::unordered_map<const llvm::Function*, CallInformation> callInformation;
	
public:
	ParameterRegistry(TargetInfo& target, Executable& executable);
	
	TargetInfo& getTarget() { return target; }
	Executable& getExecutable() { return executable; }
	
	CallingConvention* getCallingConvention(llvm::Function& function);
	const CallInformation* getCallInfo(llvm::Function& function) const;
	
	CallInformation* createCallInfo(llvm::Function& function, const char* ccName);
};

#endif /* register_use_hpp */
