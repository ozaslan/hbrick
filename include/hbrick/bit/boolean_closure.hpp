#pragma once

#include "hbrick/bit/bit_matrix.hpp"

namespace hbrick {

class BooleanClosure {
public:
    static void transitiveClosureWarshallInPlace(BitMatrix& relation);

    [[nodiscard]] static BitMatrix transitiveClosureWarshall(BitMatrix relation);
};

}  // namespace hbrick
