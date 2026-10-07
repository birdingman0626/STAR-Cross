#include "doctest/doctest.h"
#include "ParametersGenome.h"
#include <cstring>
#include <new>
#include <type_traits>

TEST_CASE("Genome transformation output flags default to false on nonzero storage") {
    // Poison storage so this does not accidentally pass because stack bytes are zero.
    std::aligned_storage<sizeof(ParametersGenome), alignof(ParametersGenome)>::type storage;
    std::memset(&storage, 0xff, sizeof(storage));
    auto *parameters = new (&storage) ParametersGenome;
    CHECK_FALSE(parameters->transform.outYes);
    CHECK_FALSE(parameters->transform.outSAM);
    CHECK_FALSE(parameters->transform.outSJ);
    CHECK_FALSE(parameters->transform.outQuant);
    parameters->~ParametersGenome();
}
