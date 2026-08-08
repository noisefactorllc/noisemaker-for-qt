#include "png_io.h"

namespace nm {

bool savePng(const QImage& image, const QString& path) {
    return image.save(path, "PNG");
}

} // namespace nm
