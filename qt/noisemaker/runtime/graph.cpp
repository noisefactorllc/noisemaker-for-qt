#include "graph.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <stdexcept>

namespace nm {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QString requireString(const QJsonObject& obj, const QString& key, const QString& context) {
    const QJsonValue value = obj.value(key);
    if (!value.isString()) {
        fail(QStringLiteral("%1: missing or non-string field '%2'").arg(context, key));
    }
    return value.toString();
}

// Returns the string at `key`, or an empty string if absent / JSON null
// (used for fields that are legitimately null for blit passes, e.g.
// "namespace"/"effectKey" -- docs/GRAPH-JSON-SCHEMA.md "Blit passes use...
// namespace:null... effectKey:null").
QString optionalString(const QJsonObject& obj, const QString& key) {
    const QJsonValue value = obj.value(key);
    return value.isString() ? value.toString() : QString();
}

TextureSpec parseTextureSpec(const QJsonValue& value, const QString& texId) {
    if (!value.isObject()) {
        fail(QStringLiteral("textures['%1']: expected an object").arg(texId));
    }
    const QJsonObject obj = value.toObject();
    if (!obj.contains(QStringLiteral("width")) || !obj.contains(QStringLiteral("height"))) {
        fail(QStringLiteral("textures['%1']: missing width/height").arg(texId));
    }

    TextureSpec spec;
    spec.width = obj.value(QStringLiteral("width"));
    spec.height = obj.value(QStringLiteral("height"));
    if (obj.contains(QStringLiteral("depth"))) {
        spec.depth = obj.value(QStringLiteral("depth"));
    }
    spec.is3D = obj.value(QStringLiteral("is3D")).toBool(false);
    spec.format = obj.value(QStringLiteral("format")).toString(QStringLiteral("rgba16f"));
    for (const QJsonValue& usageEntry : obj.value(QStringLiteral("usage")).toArray()) {
        spec.usage.append(usageEntry.toString());
    }
    return spec;
}

Pass parsePass(const QJsonValue& value, int index) {
    if (!value.isObject()) {
        fail(QStringLiteral("passes[%1]: expected an object").arg(index));
    }
    const QJsonObject obj = value.toObject();
    const QString context = QStringLiteral("passes[%1]").arg(index);

    Pass pass;
    pass.id = requireString(obj, QStringLiteral("id"), context);
    pass.passType = requireString(obj, QStringLiteral("passType"), context);
    if (pass.passType != QStringLiteral("effect") && pass.passType != QStringLiteral("blit")) {
        fail(QStringLiteral("%1: unknown passType '%2'").arg(context, pass.passType));
    }
    pass.effectNamespace = optionalString(obj, QStringLiteral("namespace"));
    pass.func = requireString(obj, QStringLiteral("func"), context);
    pass.progName = requireString(obj, QStringLiteral("progName"), context);
    pass.program = requireString(obj, QStringLiteral("program"), context);
    pass.effectKey = optionalString(obj, QStringLiteral("effectKey"));
    pass.nodeId = optionalString(obj, QStringLiteral("nodeId"));
    pass.drawMode = optionalString(obj, QStringLiteral("drawMode"));
    pass.defines = obj.value(QStringLiteral("defines")).toObject();
    pass.inputs = obj.value(QStringLiteral("inputs")).toObject();
    pass.outputs = obj.value(QStringLiteral("outputs")).toObject();
    pass.uniforms = obj.value(QStringLiteral("uniforms")).toObject();
    pass.repeat = obj.value(QStringLiteral("repeat"));
    return pass;
}

} // namespace

Graph Graph::fromJson(const QByteArray& json) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(QStringLiteral("graph JSON parse error: %1 (offset %2)")
                 .arg(parseError.errorString())
                 .arg(parseError.offset));
    }
    if (!doc.isObject()) {
        fail(QStringLiteral("graph JSON: expected a top-level object"));
    }
    const QJsonObject root = doc.object();

    Graph graph;
    graph.id = optionalString(root, QStringLiteral("id"));
    graph.source = optionalString(root, QStringLiteral("source"));
    graph.renderSurface = optionalString(root, QStringLiteral("renderSurface"));

    const QJsonValue passesValue = root.value(QStringLiteral("passes"));
    if (!passesValue.isArray()) {
        fail(QStringLiteral("graph JSON: missing or non-array 'passes'"));
    }
    const QJsonArray passesArray = passesValue.toArray();
    graph.passes.reserve(passesArray.size());
    for (int i = 0; i < passesArray.size(); ++i) {
        graph.passes.append(parsePass(passesArray.at(i), i));
    }

    const QJsonObject allocations = root.value(QStringLiteral("allocations")).toObject();
    for (auto it = allocations.begin(); it != allocations.end(); ++it) {
        graph.allocations.insert(it.key(), it.value().toString());
    }

    const QJsonObject textures = root.value(QStringLiteral("textures")).toObject();
    for (auto it = textures.begin(); it != textures.end(); ++it) {
        graph.textures.insert(it.key(), parseTextureSpec(it.value(), it.key()));
    }

    return graph;
}

} // namespace nm
