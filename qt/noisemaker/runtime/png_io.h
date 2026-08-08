#pragma once

#include <QImage>
#include <QString>

namespace nm {

// Saves `image` as a PNG to `path`. Returns false (never throws) on
// failure so callers can report a per-item error — nm-render's
// batch-manifest contract is "ERROR <out>: <why>" per item, not an
// uncaught exception.
bool savePng(const QImage& image, const QString& path);

} // namespace nm
