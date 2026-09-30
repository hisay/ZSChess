#ifndef ZSCHESS_EVALUATE_H
#define ZSCHESS_EVALUATE_H

#include "types.h"

namespace zschess {

	class Position;

	namespace Eval {

		constexpr int VALUE_INFINITE = 30000;
		constexpr int VALUE_MATE = 29000;
		constexpr int VALUE_DRAW = 0;
		constexpr int VALUE_NONE = 32000;

		int evaluate(const Position& pos);

	} // namespace Eval
} // namespace zschess

#endif
