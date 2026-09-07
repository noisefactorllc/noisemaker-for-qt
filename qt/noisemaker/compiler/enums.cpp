#include "enums.h"

namespace nm {

namespace {

// share/palettes.json key order (verbatim, 0-based positional enum; "none"
// IS index 0; 56 entries) -- verified directly against the live reference
// (NM_REFERENCE_ROOT checkout's share/palettes.json key order via `node
// -e "console.log(Object.keys(JSON.parse(...)))"`), matching godot's
// enums.gd PALETTE_KEYS transcription exactly. std_enums.js builds
// `paletteEnum` by walking `Object.keys(palettes)` in this exact order.
const QStringList& paletteKeys() {
    static const QStringList keys = {
        QStringLiteral("none"),          QStringLiteral("seventiesShirt"), QStringLiteral("fiveG"),
        QStringLiteral("afterimage"),    QStringLiteral("barstow"),        QStringLiteral("bloob"),
        QStringLiteral("blueSkies"),     QStringLiteral("brushedMetal"),   QStringLiteral("burningSky"),
        QStringLiteral("california"),    QStringLiteral("columbia"),      QStringLiteral("cottonCandy"),
        QStringLiteral("darkSatin"),     QStringLiteral("dealerHat"),     QStringLiteral("dreamy"),
        QStringLiteral("eventHorizon"),  QStringLiteral("ghostly"),       QStringLiteral("grayscale"),
        QStringLiteral("hazySunset"),    QStringLiteral("heatmap"),       QStringLiteral("hypercolor"),
        QStringLiteral("jester"),        QStringLiteral("justBlue"),      QStringLiteral("justCyan"),
        QStringLiteral("justGreen"),     QStringLiteral("justPurple"),    QStringLiteral("justRed"),
        QStringLiteral("justYellow"),    QStringLiteral("mars"),          QStringLiteral("modesto"),
        QStringLiteral("moss"),          QStringLiteral("neptune"),       QStringLiteral("netOfGems"),
        QStringLiteral("organic"),       QStringLiteral("papaya"),        QStringLiteral("radioactive"),
        QStringLiteral("royal"),         QStringLiteral("santaCruz"),     QStringLiteral("sherbet"),
        QStringLiteral("sherbetDouble"), QStringLiteral("silvermane"),    QStringLiteral("skykissed"),
        QStringLiteral("solaris"),       QStringLiteral("spooky"),        QStringLiteral("springtime"),
        QStringLiteral("sproingtime"),   QStringLiteral("sulphur"),       QStringLiteral("summoning"),
        QStringLiteral("superhero"),     QStringLiteral("toxic"),         QStringLiteral("tropicalia"),
        QStringLiteral("tungsten"),      QStringLiteral("vaporwave"),     QStringLiteral("vibrant"),
        QStringLiteral("vintage"),       QStringLiteral("vintagePhoto"),
    };
    return keys;
}

QJsonObject buildStd() {
    QJsonObject root;

    QJsonObject channel;
    channel.insert(QStringLiteral("r"), Enums::leaf(0));
    channel.insert(QStringLiteral("g"), Enums::leaf(1));
    channel.insert(QStringLiteral("b"), Enums::leaf(2));
    channel.insert(QStringLiteral("a"), Enums::leaf(3));
    root.insert(QStringLiteral("channel"), channel);

    QJsonObject color;
    color.insert(QStringLiteral("mono"), Enums::leaf(0));
    color.insert(QStringLiteral("rgb"), Enums::leaf(1));
    color.insert(QStringLiteral("hsv"), Enums::leaf(2));
    root.insert(QStringLiteral("color"), color);

    QJsonObject oscType;
    oscType.insert(QStringLiteral("sine"), Enums::leaf(0));
    oscType.insert(QStringLiteral("linear"), Enums::leaf(1));
    oscType.insert(QStringLiteral("sawtooth"), Enums::leaf(2));
    oscType.insert(QStringLiteral("sawtoothInv"), Enums::leaf(3));
    oscType.insert(QStringLiteral("square"), Enums::leaf(4));
    oscType.insert(QStringLiteral("noise1d"), Enums::leaf(5));
    oscType.insert(QStringLiteral("noise2d"), Enums::leaf(6));
    root.insert(QStringLiteral("oscType"), oscType);

    // oscKind: 'noise' is an alias of 'noise1d' -- BOTH keys carry value 5
    // (reference std_enums.js oscKindEnum comment: "periodic noise (alias
    // for noise1d)").
    QJsonObject oscKind;
    oscKind.insert(QStringLiteral("sine"), Enums::leaf(0));
    oscKind.insert(QStringLiteral("tri"), Enums::leaf(1));
    oscKind.insert(QStringLiteral("saw"), Enums::leaf(2));
    oscKind.insert(QStringLiteral("sawInv"), Enums::leaf(3));
    oscKind.insert(QStringLiteral("square"), Enums::leaf(4));
    oscKind.insert(QStringLiteral("noise"), Enums::leaf(5));
    oscKind.insert(QStringLiteral("noise1d"), Enums::leaf(5));
    oscKind.insert(QStringLiteral("noise2d"), Enums::leaf(6));
    root.insert(QStringLiteral("oscKind"), oscKind);

    QJsonObject midiMode;
    midiMode.insert(QStringLiteral("noteChange"), Enums::leaf(0));
    midiMode.insert(QStringLiteral("gateNote"), Enums::leaf(1));
    midiMode.insert(QStringLiteral("gateVelocity"), Enums::leaf(2));
    midiMode.insert(QStringLiteral("triggerNote"), Enums::leaf(3));
    midiMode.insert(QStringLiteral("velocity"), Enums::leaf(4));
    midiMode.insert(QStringLiteral("cc"), Enums::leaf(5));
    midiMode.insert(QStringLiteral("cc14"), Enums::leaf(6));
    midiMode.insert(QStringLiteral("nrpn"), Enums::leaf(7));
    midiMode.insert(QStringLiteral("pitchBend"), Enums::leaf(8));
    midiMode.insert(QStringLiteral("pressure"), Enums::leaf(9));
    midiMode.insert(QStringLiteral("polyPressure"), Enums::leaf(10));
    root.insert(QStringLiteral("midiMode"), midiMode);
    root.insert(QStringLiteral("midiZone"), QJsonObject{{QStringLiteral("lower"), Enums::leaf(0)}, {QStringLiteral("upper"), Enums::leaf(1)}});

    QJsonObject audioBand;
    audioBand.insert(QStringLiteral("low"), Enums::leaf(0));
    audioBand.insert(QStringLiteral("mid"), Enums::leaf(1));
    audioBand.insert(QStringLiteral("high"), Enums::leaf(2));
    audioBand.insert(QStringLiteral("vol"), Enums::leaf(3));
    audioBand.insert(QStringLiteral("raw"), Enums::leaf(4));
    root.insert(QStringLiteral("audioBand"), audioBand);

    QJsonObject palette;
    const QStringList& keys = paletteKeys();
    for (int i = 0; i < keys.size(); ++i) {
        palette.insert(keys.at(i), Enums::leaf(i));
    }
    root.insert(QStringLiteral("palette"), palette);

    return root;
}

// Copy-on-write recursive tree update: returns a COPY of `node` with a leaf
// installed at `path[idx..]`. At each level, an EXISTING subtree is reused
// (its own children preserved) rather than replaced -- mirrors the
// reference's deepMerge, which recurses into an existing non-leaf target
// object instead of overwriting it, so that (e.g.) registering
// filter.warp.amount after filter.blur.mode does not delete the earlier
// filter.blur subtree. An existing LEAF at an intermediate level (a
// namespace/func/key segment colliding with a choice name) is treated as
// "start fresh" rather than the reference's own stranger behavior of
// recursing into the leaf object itself -- structurally unreachable here,
// since registerChoice is always called with the fixed 4-deep
// [ns,func,key,choiceName] shape (ns/func/key segments are never
// themselves registered as leaves).
QJsonObject insertLeaf(const QJsonObject& node, const QStringList& path, int idx, const QJsonValue& value) {
    QJsonObject out = node;
    if (idx == path.size() - 1) {
        out.insert(path.at(idx), Enums::leaf(value));
        return out;
    }
    const QJsonValue existing = out.value(path.at(idx));
    const QJsonObject child = (existing.isObject() && !Enums::isLeaf(existing)) ? existing.toObject() : QJsonObject();
    out.insert(path.at(idx), insertLeaf(child, path, idx + 1, value));
    return out;
}

} // namespace

Enums::Enums() : std_(buildStd()) {}

QJsonObject Enums::leaf(const QJsonValue& value) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("Number"));
    o.insert(QStringLiteral("value"), value);
    return o;
}

bool Enums::isLeaf(const QJsonValue& node) {
    if (!node.isObject()) return false;
    const QJsonValue t = node.toObject().value(QStringLiteral("type"));
    return t.isString() && t.toString() == QStringLiteral("Number");
}

QJsonValue Enums::tryGetHead(const QString& head) const {
    if (project_.contains(head)) return project_.value(head);
    if (std_.contains(head)) return std_.value(head);
    // NOTE: QJsonValue's own default constructor yields Type::Null, NOT
    // Type::Undefined (verified directly -- only QJsonObject::value() on a
    // MISSING key, or this explicit tag constructor, produce Undefined).
    // Every "not found" sentinel in this compiler must use this exact
    // form, never a bare QJsonValue().
    return QJsonValue(QJsonValue::Undefined);
}

void Enums::registerChoice(const QStringList& path, const QJsonValue& value) {
    if (path.isEmpty()) return;
    project_ = insertLeaf(project_, path, 0, value);
}

} // namespace nm
