// Unit tests for nm::js::numberToString (qt/noisemaker/compiler/
// js_number.{h,cpp}): String(number) as V8 prints it. Plain assert-style
// checks, no test framework dependency.
//
// Each expected string was printed by `String(x)` in node 26.10.0. The
// cases cover each layout branch of the spec's Number::toString (plain
// integer digits up to 21, a decimal point inside the digits, leading
// "0.000", exponent form on both sides), its boundaries, and shortest
// round-trip digits. The DSL-level behaviour is oracle-gated by the
// compiler gates (check_validate compares diagnostic text byte for byte).

#include "../noisemaker/compiler/js_number.h"

#include <QString>

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int g_failures = 0;

void check(double value, const char* expected, const char* source) {
    const QString actual = nm::js::numberToString(value);
    const bool ok = actual == QLatin1String(expected);
    std::printf("%s: String(%s) -> %s%s%s\n", ok ? "PASS" : "FAIL", source, expected, ok ? "" : ", got ",
                ok ? "" : actual.toUtf8().constData());
    if (!ok) ++g_failures;
}

} // namespace

int main() {
    check(0.0, "0", "0");
    check(-0.0, "0", "-0");
    check(1.0, "1", "1");
    check(-1.0, "-1", "-1");
    check(0.1, "0.1", "0.1");
    check(-0.5, "-0.5", "-0.5");
    check(1.5, "1.5", "1.5");
    check(100.0, "100", "100");
    check(123.456, "123.456", "123.456");
    check(1.0 / 3.0, "0.3333333333333333", "1/3");
    check(2.0 / 3.0, "0.6666666666666666", "2/3");
    check(1e21, "1e+21", "1e21");
    check(1e20, "100000000000000000000", "1e20");
    check(123456789012345680000.0, "123456789012345680000", "123456789012345680000");
    check(1.2345678901234568e+21, "1.2345678901234568e+21", "1.2345678901234568e+21");
    check(9.999999999999999e20, "999999999999999900000", "9.999999999999999e20");
    check(12345678901234567890.0, "12345678901234567000", "12345678901234567890");
    check(1e-6, "0.000001", "1e-6");
    check(0.0000012345, "0.0000012345", "0.0000012345");
    check(1e-7, "1e-7", "1e-7");
    check(1.5e-7, "1.5e-7", "1.5e-7");
    check(-2.5e-10, "-2.5e-10", "-2.5e-10");
    check(1e-5 * 3, "0.000030000000000000004", "1e-5*3");
    check(5e-324, "5e-324", "5e-324");
    check(1.7976931348623157e308, "1.7976931348623157e+308", "1.7976931348623157e308");
    check(1e300, "1e+300", "1e+300");
    check(-1e-300, "-1e-300", "-1e-300");
    check(9007199254740992.0, "9007199254740992", "2**53");
    check(9007199254740994.0, "9007199254740994", "2**53+2");
    check(0.1 + 0.2, "0.30000000000000004", "0.1+0.2");
    check(255.0 / 256.0, "0.99609375", "255/256");
    check(3.141592653589793, "3.141592653589793", "Math.PI");
    check(2.718281828459045, "2.718281828459045", "Math.E");
    check(4.35, "4.35", "4.35");
    check(0.3, "0.3", "0.3");
    check(std::numeric_limits<double>::quiet_NaN(), "NaN", "NaN");
    check(std::numeric_limits<double>::infinity(), "Infinity", "Infinity");
    check(-std::numeric_limits<double>::infinity(), "-Infinity", "-Infinity");

    if (g_failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("FAILED (%d failures)\n", g_failures);
    return 1;
}
