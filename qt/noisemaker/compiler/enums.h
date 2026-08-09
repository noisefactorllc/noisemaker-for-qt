#pragma once

// enums.h -- stdEnums tree + dynamic project-enum registry. Port of the
// REFERENCE shaders/src/lang/std_enums.js + enums.js (source of truth),
// cross-checked against td/noisemaker/compiler/lang/enums.py and
// godot/addons/noisemaker/compiler/lang/enums.gd. See enums.cpp for the
// PARITY-CRITICAL details (palette positional indices, oscKind aliasing).
//
// An enum tree node is either a SUBTREE (QJsonObject of name -> node) or a
// LEAF ({"type":"Number","value":<double>} -- a `type` key distinguishes a
// leaf from a subtree, mirroring the reference deepMerge's `!('type' in
// sourceVal)` check). Two independent trees:
//   - std()     the FIXED table built once at construction (channel/color/
//               oscType/oscKind/midiMode/audioBand/palette, verbatim from
//               std_enums.js). It exists regardless of whether any effect
//               or `mergeIntoEnums(stdEnums)` call ever runs, because the
//               reference's stdEnums is a plain object import validator.js
//               consults directly -- never routed through the mutable
//               enums.js merge machinery.
//   - project() the DYNAMIC tree, populated ONLY from effect `choices`
//               registered via registerChoice() (EffectRegistry). This is
//               the ONLY tree parity/check_registry.mjs's "enums" section
//               compares (tools/dump-registry.mjs never merges stdEnums
//               into it -- see its header comment).

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace nm {

class Enums {
public:
    Enums();

    // {"type":"Number","value":value} -- the leaf shape every enum entry
    // collapses to. `value` is usually a double, but NOT always: the
    // reference's registration (canvas.js choicesToRegister) blindly wraps
    // WHATEVER a `choices` entry's value is into {type:'Number', value}
    // with no type check -- filter.text's `font`/`justify` globals are
    // `type:"string"` with STRING-valued choices ({"nunito":"Nunito", ...})
    // and still register as {"type":"Number","value":"Nunito"} (verified
    // against the live oracle). Ported bug-for-bug: the "type" tag is
    // always the literal string "Number" regardless of the JSON value's
    // real type.
    static QJsonObject leaf(const QJsonValue& value);
    // True iff `node` is an object shaped like leaf() above (a `type`
    // key equal to the string "Number" distinguishes a leaf from a
    // subtree, exactly as the reference's deepMerge/isLeaf checks do).
    static bool isLeaf(const QJsonValue& node);

    const QJsonObject& std() const { return std_; }
    const QJsonObject& project() const { return project_; }

    // Top-level head lookup for path resolution: project before std
    // (reference/02 resolveEnum precedence -- Validator::resolveEnum
    // walks the rest of the path itself). Undefined (QJsonValue::
    // isUndefined()) if `head` is registered in neither tree.
    QJsonValue tryGetHead(const QString& head) const;

    // Install a nested leaf at `path` (e.g. ["filter","blur","mode",
    // "gaussian"]) into project(), creating intermediate subtrees as
    // needed and preserving every sibling already registered at each
    // level (mirrors the reference's deepMerge, which recurses into an
    // existing subtree rather than overwriting it -- see enums.cpp).
    void registerChoice(const QStringList& path, const QJsonValue& value);

private:
    QJsonObject std_;
    QJsonObject project_;
};

} // namespace nm
