#pragma once
#ifdef ENABLE_CUSTOM_FORMULA
#include <symengine/expression.h>
#include <mpc.h>

class JITEvaluator : public SimpleEvaluator {
	struct JITReference : Reference {
		SRReal Refcr, Refci;
		IntIter RefIteration;

		__m128d *Ref = nullptr;

		virtual ~JITReference() override;
	};

	SymEngine::Expression expr = SymEngine::Expression("z*z+c");
	SymEngine::Expression CriticalPoint;
	bool NonZeroCriticalPoint = false;

	using FuncPointer = uint64_t (*)(__m128d &z, const __m128d &c, uint64_t MaxIt);
	using FuncPointerMpc = void (*)(mpc_ptr z, mpc_srcptr c, mpc_t *temp);
	using FuncPointerRef = void (*)(mpc_ptr z, mpc_srcptr c, mpc_t *temp, const __m128d *Ref);
	using FuncPointerPt = void (*)(__m128d *Variables, const __m128d *Constants, const __m128d &Reference);

	uint8_t *Buffer = nullptr;
	FuncPointer func = nullptr;
	FuncPointerMpc funcMpc = nullptr;
	FuncPointerRef funcRef = nullptr;
	FuncPointerPt funcPt = nullptr;
	size_t ReferenceElementSize;

public:
	JITEvaluator();
	virtual ~JITEvaluator() override;

	virtual Reference *GenerateReference(const EvaluationParameters &parameters) override;
	virtual void Evaluate(const EvaluationParameters &parameters, PixelManager &rasterizer, Reference *Reference) override;
};
#endif