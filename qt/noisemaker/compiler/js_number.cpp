#include "js_number.h"

#include <QLocale>

#include <cmath>

namespace nm::js {

QString numberToString(double value) {
    if (std::isnan(value)) return QStringLiteral("NaN");
    if (value == 0.0) return QStringLiteral("0");
    if (std::isinf(value)) return value > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");

    // Qt's shortest round-trip conversion (its bundled double-conversion,
    // the algorithm V8 uses) in exponent form, e.g. "1.5e-07": the digits
    // s and the exponent e of the value s x 10^(e - k + 1), k = digit count.
    const QString exponential = QString::number(std::fabs(value), 'e', QLocale::FloatingPointShortest);
    const qsizetype e = exponential.indexOf(QLatin1Char('e'));
    QString digits = exponential.left(e);
    digits.remove(QLatin1Char('.'));
    const int k = static_cast<int>(digits.size());
    const int n = exponential.mid(e + 1).toInt() + 1; // the spec's n: value = 0.d1..dk x 10^n

    QString out = value < 0 ? QStringLiteral("-") : QString();
    if (k <= n && n <= 21) {
        out += digits + QString(n - k, QLatin1Char('0'));
    } else if (0 < n && n <= 21) {
        out += digits.left(n) + QLatin1Char('.') + digits.mid(n);
    } else if (-6 < n && n <= 0) {
        out += QStringLiteral("0.") + QString(-n, QLatin1Char('0')) + digits;
    } else {
        out += digits.left(1);
        if (k > 1) out += QLatin1Char('.') + digits.mid(1);
        out += QLatin1Char('e') + QString(n - 1 >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
               + QString::number(std::abs(n - 1));
    }
    return out;
}

} // namespace nm::js
