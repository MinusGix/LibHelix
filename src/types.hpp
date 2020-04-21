#pragma once

#include <type_safe/strong_typedef.hpp>

namespace ts = type_safe;

using GeneralOffset = size_t;
using GeneralPosition = size_t;

struct Relative :
    ts::strong_typedef<Relative, GeneralOffset>,
    ts::strong_typedef_op::decrement<Relative>,
    ts::strong_typedef_op::equality_comparison<Relative>,
    ts::strong_typedef_op::increment<Relative> {
    using strong_typedef::strong_typedef;
};

/// Absolute position into the file
/// Should (usually) not be directly used, as this is after modifications (such as constraints)
/// No math allowed on it, as that might violate the constrains applied to it
struct Absolute : ts::strong_typedef<Absolute, GeneralPosition>,
    ts::strong_typedef_op::equality_comparison<Absolute>,
    ts::strong_typedef_op::relational_comparison<Absolute>,
    // I don't really like this
    ts::strong_typedef_op::addition<Absolute>,
    ts::strong_typedef_op::subtraction<Absolute> {
    using strong_typedef::strong_typedef;
};

/// The natural position into the file. This is before constrains are applied and is what should usually be used.
struct Natural :
    ts::strong_typedef<Natural, GeneralPosition>,
    ts::strong_typedef_op::equality_comparison<Natural>,
    ts::strong_typedef_op::relational_comparison<Natural>,
    ts::strong_typedef_op::decrement<Natural>,
    ts::strong_typedef_op::increment<Natural>,
    ts::strong_typedef_op::mixed_addition<Natural, Relative>,
    ts::strong_typedef_op::mixed_subtraction<Natural, Relative>,
    ts::strong_typedef_op::subtraction<Natural>,
    ts::strong_typedef_op::modulo<Natural> {
    using strong_typedef::strong_typedef;
};