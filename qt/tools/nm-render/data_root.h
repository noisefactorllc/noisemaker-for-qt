#pragma once

// The data root (shaders/, effects/, fonts/, share/) shared by every
// nm-render mode, in order:
//   1. NOISEMAKER_QT_DATA_ROOT, when set (used as given; a wrong value is
//      reported by EffectRegistry::loadAll, which names the directory);
//   2. an install layout, <prefix>/bin/nm-render with the data in
//      <prefix>/share/noisemaker-qt/noisemaker (the install rules' path);
//   3. the source tree: qt/build/nm-render next to qt/noisemaker;
//   4. qt/noisemaker under the working directory (a build directory
//      elsewhere, run from the repository root).
// Falls back to the relative "qt/noisemaker" so the error names it.

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QtGlobal>

namespace nm {

inline QString resolveDataRoot() {
    const QString fromEnv = qEnvironmentVariable("NOISEMAKER_QT_DATA_ROOT");
    if (!fromEnv.isEmpty()) return fromEnv;

    const QDir exeDir(QCoreApplication::applicationDirPath());
    const QString installed = QDir::cleanPath(exeDir.absoluteFilePath(QStringLiteral("../share/noisemaker-qt/noisemaker")));
    if (QDir(installed).exists(QStringLiteral("shaders"))) return installed;

    const QString sourceTree = QDir::cleanPath(exeDir.absoluteFilePath(QStringLiteral("../noisemaker")));
    if (QDir(sourceTree).exists(QStringLiteral("shaders"))) return sourceTree;

    const QDir fromCwd(QStringLiteral("qt/noisemaker"));
    if (fromCwd.exists(QStringLiteral("shaders"))) return fromCwd.absolutePath();

    return QStringLiteral("qt/noisemaker");
}

} // namespace nm
