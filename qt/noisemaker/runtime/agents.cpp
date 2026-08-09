#include "agents.h"

#include <QOpenGLFunctions_4_1_Core>

namespace nm {

bool isAgentDrawMode(const QString& drawMode) {
    return drawMode == QStringLiteral("points") || drawMode == QStringLiteral("billboards");
}

int agentVertexCount(const QString& drawMode, int agentCount) {
    return drawMode == QStringLiteral("billboards") ? agentCount * 6 : agentCount;
}

QStringList agentStateSamplerPriority() {
    return {QStringLiteral("xyzTex"), QStringLiteral("stateTex1"), QStringLiteral("inputTex")};
}

unsigned int resolveBlendFactor(const QString& name) {
    if (name == QStringLiteral("ONE")) return GL_ONE;
    if (name == QStringLiteral("ZERO")) return GL_ZERO;
    if (name == QStringLiteral("SRC_COLOR")) return GL_SRC_COLOR;
    if (name == QStringLiteral("ONE_MINUS_SRC_COLOR")) return GL_ONE_MINUS_SRC_COLOR;
    if (name == QStringLiteral("DST_COLOR")) return GL_DST_COLOR;
    if (name == QStringLiteral("ONE_MINUS_DST_COLOR")) return GL_ONE_MINUS_DST_COLOR;
    if (name == QStringLiteral("SRC_ALPHA")) return GL_SRC_ALPHA;
    if (name == QStringLiteral("ONE_MINUS_SRC_ALPHA")) return GL_ONE_MINUS_SRC_ALPHA;
    if (name == QStringLiteral("DST_ALPHA")) return GL_DST_ALPHA;
    if (name == QStringLiteral("ONE_MINUS_DST_ALPHA")) return GL_ONE_MINUS_DST_ALPHA;
    if (name == QStringLiteral("CONSTANT_COLOR")) return GL_CONSTANT_COLOR;
    if (name == QStringLiteral("ONE_MINUS_CONSTANT_COLOR")) return GL_ONE_MINUS_CONSTANT_COLOR;
    if (name == QStringLiteral("CONSTANT_ALPHA")) return GL_CONSTANT_ALPHA;
    if (name == QStringLiteral("ONE_MINUS_CONSTANT_ALPHA")) return GL_ONE_MINUS_CONSTANT_ALPHA;
    if (name == QStringLiteral("SRC_ALPHA_SATURATE")) return GL_SRC_ALPHA_SATURATE;
    // reference webgl2.js resolveBlendFactor: `return factors[factor] || gl.ONE`.
    return GL_ONE;
}

} // namespace nm
