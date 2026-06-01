#include "hbrick/bit/boolean_closure.hpp"

namespace hbrick {

void BooleanClosure::transitiveClosureWarshallInPlace(BitMatrix& relation) {
    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols()) {
        return;
    }

    for (uint32_t pivot = 0; pivot < num_vertices; ++pivot) {
        for (uint32_t row_index = 0; row_index < num_vertices; ++row_index) {
            if (relation.test(row_index, pivot)) {
                relation.row(row_index).rowOr(relation.row(pivot));
            }
        }
    }
}

BitMatrix BooleanClosure::transitiveClosureWarshall(BitMatrix relation) {
    transitiveClosureWarshallInPlace(relation);
    return relation;
}

}  // namespace hbrick
