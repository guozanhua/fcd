//
// pass_conditions.cpp
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

#include "passes.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/PatternMatch.h>
SILENCE_LLVM_WARNINGS_END()

using namespace llvm;
using namespace llvm::PatternMatch;
using namespace std;

namespace
{
	Value* getOriginalValue(Value& value)
	{
		Value* from = &value;
		while (auto asCast = dyn_cast<CastInst>(from))
		{
			from = asCast->getOperand(0);
		}
		return from;
	}
	
	bool isSameLowerBits(Value* a, Value* b)
	{
		return getOriginalValue(*a) == getOriginalValue(*b);
	}
	
	struct Subtraction
	{
		Value* left;
		Value* right;
		unsigned bitness;
		
		Subtraction(Value* left, Value* right, unsigned bitness)
		: left(left), right(right), bitness(bitness)
		{
		}
		
		bool isSame(Value* l, Value* r, unsigned b)
		{
			if (b == bitness)
			{
				return (isSameLowerBits(l, left) && isSameLowerBits(r, right))
					|| (isSameLowerBits(r, left) && isSameLowerBits(l, right));
			}
			return false;
		}
	};
	
	bool isSameSub(unique_ptr<Subtraction>& sub, Value* a, Value* b, unsigned bitness)
	{
		if (sub)
		{
			return sub->isSame(a, b, bitness);
		}
		else
		{
			sub.reset(new Subtraction(a, b, bitness));
			return true;
		}
	}
	
	bool matchSignFlag(Value& value, unique_ptr<Subtraction>& sub)
	{
		auto original = getOriginalValue(value);
		Value* one = nullptr;
		if (match(original, m_And(m_Value(original), m_Value(one))))
		if (match(getOriginalValue(*one), m_ConstantInt<1>()))
		{
			original = getOriginalValue(*original);
		}
		
		Value* a = nullptr;
		Value* b = nullptr;
		Value* operand = nullptr;
		ConstantInt* shiftAmount = nullptr;
		if (match(original, m_LShr(m_Value(operand), m_ConstantInt(shiftAmount))))
		{
			auto bitness = static_cast<unsigned>(shiftAmount->getLimitedValue()) + 1;
			if (match(operand, m_Sub(m_Value(a), m_Value(b))))
			{
				return isSameSub(sub, a, b, bitness);
			}
		}
		return false;
	}
	
	bool matchOverflowFlag(Value& value, unique_ptr<Subtraction>& sub)
	{
		auto original = getOriginalValue(value);
		if (auto extract = dyn_cast<ExtractValueInst>(original))
		if (auto intrin = dyn_cast<IntrinsicInst>(extract->getAggregateOperand()))
		if (intrin->getIntrinsicID() == Intrinsic::ssub_with_overflow)
		{
			auto indices = extract->getIndices();
			if (indices.size() == 1 && indices[0] == 1)
			{
				Value* a = intrin->getArgOperand(0);
				return isSameSub(sub, a, intrin->getArgOperand(1), a->getType()->getIntegerBitWidth());
			}
		}
		return false;
	}
	
	unique_ptr<Subtraction> matchOverflowSignFlag(Value& xorLeft, Value& xorRight)
	{
		unique_ptr<Subtraction> sub;
		if (matchOverflowFlag(xorLeft, sub))
		{
			if (matchSignFlag(xorRight, sub))
			{
				return sub;
			}
		}
		else if (matchOverflowFlag(xorRight, sub))
		{
			if (matchSignFlag(xorLeft, sub))
			{
				return sub;
			}
		}
		return nullptr;
	}
	
	struct ConditionSimplification final : public FunctionPass
	{
		static char ID;
		
		ConditionSimplification() : FunctionPass(ID)
		{
		}
		
		virtual bool runOnFunction(Function& fn) override
		{
			bool result = false;
			for (auto& bb : fn)
			{
				result |= runOnBasicBlock(bb);
			}
			return result;
		}
		
		bool runOnBasicBlock(BasicBlock& bb)
		{
			bool result = false;
			for (auto& inst : bb)
			{
				Value* arg0 = nullptr;
				Value* arg1 = nullptr;
				ICmpInst::Predicate pred;
				if (match(&inst, m_Intrinsic<Intrinsic::usub_with_overflow>(m_Value(arg0), m_Value(arg1))))
				{
					for (auto user : inst.users())
					{
						if (auto extract = dyn_cast<ExtractValueInst>(user))
						{
							auto indices = extract->getIndices();
							if (indices.size() == 1 && indices[0] == 1)
							{
								auto icmp = ICmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_ULT, arg0, arg1, "", &inst);
								extract->replaceAllUsesWith(icmp);
							}
						}
					}
				}
				else if (match(&inst, m_Xor(m_Value(arg0), m_Value(arg1))) || (match(&inst, m_ICmp(pred, m_Value(arg0), m_Value(arg1))) && pred == ICmpInst::ICMP_NE))
				{
					if (unique_ptr<Subtraction> sub = matchOverflowSignFlag(*arg0, *arg1))
					{
						auto icmp = ICmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_SLT, sub->left, sub->right, "", &inst);
						auto zext = CastInst::Create(CastInst::ZExt, icmp, inst.getType(), "", &inst);
						inst.replaceAllUsesWith(zext);
					}
				}
			}
			return result;
		}
	};
	
	char ConditionSimplification::ID = 0;
	
	RegisterPass<ConditionSimplification> condSimp("simplifyconditions", "Simplify flag-based x86 conditionals");
}

FunctionPass* createConditionSimplificationPass()
{
	return new ConditionSimplification;
}
