#pragma once

// --time and --frames values, shared by the nm-render render paths that take
// them (--dsl, --graph, --batch-manifest). The rules are the export kit
// renderer's (export-kit/kit/src/main.cpp): a time is a number T with
// 0 <= T < 1, and a frame count is a positive integer. An absent value keeps
// its default (time 0, one frame).

#include <QJsonValue>
#include <QString>

namespace nm {

inline bool isLoopTime(double time) {
    return time >= 0.0 && time < 1.0;
}

// `text` from the command line; empty means absent.
inline bool parseTimeArg(const QString& text, double* out) {
    if (text.isEmpty()) {
        *out = 0.0;
        return true;
    }
    bool ok = false;
    *out = text.toDouble(&ok);
    return ok && isLoopTime(*out);
}

inline bool parseFramesArg(const QString& text, int* out) {
    if (text.isEmpty()) {
        *out = 1;
        return true;
    }
    bool ok = false;
    *out = text.toInt(&ok);
    return ok && *out >= 1;
}

// `value` from a batch manifest item; undefined means the key is absent.
inline bool parseTimeValue(const QJsonValue& value, double* out) {
    if (value.isUndefined()) {
        *out = 0.0;
        return true;
    }
    *out = value.toDouble();
    return value.isDouble() && isLoopTime(*out);
}

inline bool parseFramesValue(const QJsonValue& value, int* out) {
    if (value.isUndefined()) {
        *out = 1;
        return true;
    }
    // toInt() gives the fallback 0 for a fractional or out-of-range number.
    *out = value.toInt(0);
    return value.isDouble() && *out >= 1;
}

} // namespace nm
