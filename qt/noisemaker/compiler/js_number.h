#pragma once

// js_number.h -- ECMAScript Number::toString (String(number), `${number}`).

#include <QString>

namespace nm::js {

// The shortest decimal that round-trips to `value`, laid out as the
// ECMAScript spec's Number::toString(x, 10): plain digits while the decimal
// exponent is in [-6, 21), otherwise exponent form such as "1e+21" or
// "1.5e-7". -0 prints as "0". Needs no C++17 floating-point std::to_chars,
// which Apple's libc++ only provides from macOS 13.3.
QString numberToString(double value);

} // namespace nm::js
