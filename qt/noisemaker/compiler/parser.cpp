#include "parser.h"

#include "ast.h"
#include "diagnostics.h"
#include "tokens.h"

#include <QSet>
#include <QStringList>
#include <QVector>

#include <utility>

namespace nm {

namespace {

// PARITY-CRITICAL behaviors replicated exactly from the reference
// shaders/src/lang/parser.js (source of truth), cross-checked against
// td/noisemaker/compiler/lang/parser.py and
// godot/addons/noisemaker/compiler/lang/parser.gd, and spot-verified
// against the reference oracle (NM_REFERENCE_ROOT tools/dump-ast.mjs) for
// every hazard called out below -- see task report "hazards encountered":
//   - Numeric +-*/ are CONSTANT-FOLDED at parse time in double,
//     left-to-right within a precedence level; operands MUST be Number
//     literals (else "Expected number"). Math.PI = 3.141592653589793.
//   - HEX color: int(pair,16)/255 in double; 3-digit char duplication;
//     alpha default 1.0; 8-digit form's alpha is the 4th pair / 255.
//   - Special-form transforms in this exact order: from, osc (4-way
//     heuristic), midi, audio, read, read3d.
//   - `search` mandatory + position-restricted (only before any other
//     statement); `render` terminates the program's statement loop
//     entirely (so the reference's "Duplicate render()" check is
//     unreachable dead code -- ported anyway for fidelity, but verified
//     via the oracle that two `render()` calls in one program instead
//     hits "Expected end of input").
//   - A DOTTED name immediately followed (across 1+ member segments) by a
//     call is ALWAYS routed through parseChain/parseCall, which then
//     unconditionally expects LPAREN right after the identifier: exactly
//     one dot before the call -> explicit "inline namespace syntax ...
//     is not allowed"; two or more dots before the call -> the generic
//     "Expect '('" (this second case is a reference bug-for-bug, verified
//     against the oracle: `foo.bar.baz()` throws "Expect '(' at line 1
//     col 4", NOT a namespace error and NOT a successful Member parse).
//   - Comments are legal ONLY where the reference explicitly calls
//     collectComments(): between top-level program statements, around
//     the '.' in a chain, and before each subchain-body element. A
//     comment immediately before a statement INSIDE an if/elif/else
//     block is a syntax error (verified against the oracle).
//   - JS falsy-OR quirk: subchain `name`/`id` use `kwargs.x?.value ||
//     null`, so an explicitly EMPTY STRING value also collapses to JSON
//     null, not "" (verified against the oracle) -- do not "fix" this.
//   - Read.surface / Read3D.tex3d are OMITTED entirely (no key at all)
//     when unresolved, matching the reference's bare `surface: surface`
//     / `tex3d: tex3d` (no `|| null` fallback) -- but Read3D.geo DOES
//     have `|| null` in the reference and is therefore ALWAYS present,
//     verified against the oracle for bare `read()` / `read3d()`.
//   - `search`'s VALID_NAMESPACES is the reference's frozen BUILT-IN set
//     (shaders/src/runtime/tags.js _builtinDescriptors) in declaration
//     order -- dump-ast.mjs only imports lexer.js+parser.js, so no effect
//     module ever calls registerNamespace() in the oracle's process, and
//     this exact 10-name list is all that is ever valid there (matches
//     godot/addons/noisemaker/compiler/lang/tags.gd's hard-coded list).

const QStringList& validNamespaces() {
    static const QStringList list = {
        QStringLiteral("io"),      QStringLiteral("classicNoisedeck"), QStringLiteral("synth"),
        QStringLiteral("mixer"),   QStringLiteral("filter"),           QStringLiteral("render"),
        QStringLiteral("points"),  QStringLiteral("synth3d"),          QStringLiteral("filter3d"),
        QStringLiteral("user"),
    };
    return list;
}

bool isValidNamespaceName(const QString& ns) {
    return validNamespaces().contains(ns);
}

// Token types that can begin an expression (reference exprStartTokens).
const QSet<QString>& exprStartTokens() {
    static const QSet<QString> s = {
        TokenType::PLUS,       TokenType::MINUS,     TokenType::NUMBER,     TokenType::HEX,
        TokenType::FUNC,       TokenType::STRING,    TokenType::IDENT,      TokenType::OUTPUT_REF,
        TokenType::SOURCE_REF, TokenType::VOL_REF,   TokenType::GEO_REF,    TokenType::MESH_REF,
        TokenType::XYZ_REF,    TokenType::VEL_REF,   TokenType::RGBA_REF,   TokenType::LPAREN,
        TokenType::LBRACKET,   TokenType::TRUE,      TokenType::FALSE,
    };
    return s;
}

// Token types allowed as segments inside a dotted member/enum path
// (reference memberTokenTypes).
const QSet<QString>& memberTokenTypes() {
    static const QSet<QString> s = {
        TokenType::IDENT,   TokenType::SOURCE_REF, TokenType::OUTPUT_REF, TokenType::VOL_REF,
        TokenType::GEO_REF, TokenType::MESH_REF,   TokenType::XYZ_REF,    TokenType::VEL_REF,
        TokenType::RGBA_REF, TokenType::LET,       TokenType::RENDER,     TokenType::TRUE,
        TokenType::FALSE,   TokenType::IF,         TokenType::ELIF,       TokenType::ELSE,
        TokenType::BREAK,   TokenType::CONTINUE,   TokenType::RETURN,     TokenType::WRITE,
        TokenType::WRITE3D, TokenType::SUBCHAIN,
    };
    return s;
}

// Token types usable as a namespace identifier in a search directive
// (reference namespaceTokenTypes -- keywords are valid namespace names).
const QSet<QString>& namespaceTokenTypes() {
    static const QSet<QString> s = {
        TokenType::IDENT,  TokenType::RENDER,   TokenType::WRITE,    TokenType::WRITE3D,
        TokenType::TRUE,   TokenType::FALSE,    TokenType::IF,       TokenType::ELIF,
        TokenType::ELSE,   TokenType::BREAK,    TokenType::CONTINUE, TokenType::RETURN,
    };
    return s;
}

QJsonObject refNode(const QString& type, const QString& name) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("name"), name);
    return o;
}

int hexPairToInt(const QString& pair) {
    bool ok = false;
    const int v = pair.toInt(&ok, 16);
    return ok ? v : 0;
}

class Parser {
public:
    explicit Parser(QVector<Token> tokens) : tokens_(std::move(tokens)) {}

    QJsonObject parseProgram();

private:
    QVector<Token> tokens_;
    int current_ = 0;

    // Track the search order for the program (set by the search
    // directive -- REQUIRED). hasSearch_ mirrors the reference's
    // `programSearchOrder !== null`.
    bool hasSearch_ = false;
    QStringList searchOrder_;
    QJsonArray namespaceImports_;
    QJsonObject namespaceDefault_;
    bool hasNamespaceDefault_ = false;

    // --- cursor helpers ---------------------------------------------
    bool inBounds(int idx) const { return idx >= 0 && idx < tokens_.size(); }
    // Clamped read (defensive: the reference's `undefined` semantics for
    // an out-of-range peek() never trigger on a well-formed grammar walk,
    // but clamping to the trailing EOF token avoids any UB if one ever
    // did -- same defensive posture as the godot port's _peek()).
    const Token& peek() const { return tokens_.at(current_ < tokens_.size() ? current_ : tokens_.size() - 1); }
    Token advance() {
        const Token t = peek();
        current_++;
        return t;
    }
    QString typeAt(int idx) const { return inBounds(idx) ? tokens_.at(idx).type : QString(); }
    const Token* tokenAt(int idx) const { return inBounds(idx) ? &tokens_.at(idx) : nullptr; }
    Token expect(const QString& type, const QString& msg) {
        const Token t = peek();
        if (t.type == type) return advance();
        throw DslSyntaxError::at(msg, t.line, t.col);
    }

    // Collect and consume any pending COMMENT tokens; returns their
    // lexemes as a JSON array of strings.
    QJsonArray collectComments() {
        QJsonArray comments;
        while (peek().type == TokenType::COMMENT) {
            comments.append(advance().lexeme);
        }
        return comments;
    }

    bool hasCallAfterDot(int index) const;

    QJsonObject parseRenderDirective();
    void parseSearchDirective();
    void validateNamespace(const Token& token);
    QJsonArray parseBlock();
    QJsonObject parseStatement();
    QJsonArray parseChain(const QString& context);
    QJsonObject parseWriteCall();
    QJsonObject parseSubchainCall();
    QJsonObject parseCall();
    QJsonObject parseArg() { return parseAdditive(); }
    void parseKwarg(QJsonObject& obj);
    QJsonObject parseAdditive();
    QJsonObject parseMultiplicative();
    QJsonObject parseUnary();
    QJsonObject parsePrimary();
    static double toNumber(const QJsonObject& node);

    QJsonObject transformOscInvocation(const QJsonObject& call, const Token& nameToken);
    QJsonObject transformMidiInvocation(const QJsonObject& call, const Token& nameToken,
                                        const QStringList& kwargOrder);
    QJsonObject transformAudioInvocation(const QJsonObject& call, const Token& nameToken,
                                         const QStringList& kwargOrder);
    QJsonObject transformFromInvocation(const QJsonObject& call, const Token& nameToken);
};

bool Parser::hasCallAfterDot(int index) const {
    int i = index + 1;
    if (typeAt(i) != TokenType::DOT) return false;
    while (typeAt(i) == TokenType::DOT) {
        const Token* seg = tokenAt(i + 1);
        if (!seg || !memberTokenTypes().contains(seg->type)) return false;
        i += 2;
    }
    return typeAt(i) == TokenType::LPAREN;
}

QJsonObject Parser::parseRenderDirective() {
    advance();
    expect(TokenType::LPAREN, QStringLiteral("Expect '('"));
    if (peek().type != TokenType::OUTPUT_REF) {
        // Reference throws with NO location suffix here.
        throw DslSyntaxError(QStringLiteral("Expected output reference in render()"));
    }
    QJsonObject out = refNode(NodeKind::OutputRef, advance().lexeme);
    expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));
    return out;
}

QJsonObject Parser::parseProgram() {
    QJsonArray plans;
    QJsonArray vars;
    QJsonObject render;
    bool hasRender = false;
    QJsonArray trailingComments;

    while (peek().type != TokenType::EOF_) {
        if (peek().type == TokenType::SEMICOLON) {
            advance();
            continue;
        }
        const QJsonArray leadingComments = collectComments();
        if (peek().type == TokenType::EOF_) {
            for (const QJsonValue& c : leadingComments) trailingComments.append(c);
            break;
        }
        // A semicolon right after a comment routes back to the top of the
        // loop (which then advances it) rather than falling into the
        // SEARCH/RENDER/statement checks below -- matches the reference's
        // otherwise-identical `if (peek().type === 'SEMICOLON') { continue }`.
        if (peek().type == TokenType::SEMICOLON) {
            continue;
        }
        if (peek().type == TokenType::SEARCH) {
            if (!plans.isEmpty() || !vars.isEmpty() || hasRender) {
                const Token t = peek();
                throw DslSyntaxError::at(QStringLiteral("'search' directive must appear before other statements"),
                                          t.line, t.col);
            }
            parseSearchDirective();
            continue;
        }
        if (peek().type == TokenType::RENDER) {
            // consumeRender() inlined: the reference's "Duplicate render()"
            // guard is unreachable given the unconditional `break` right
            // after (see file header note) -- ported for structural
            // fidelity anyway.
            if (hasRender) {
                const Token t = peek();
                throw DslSyntaxError::at(QStringLiteral("Duplicate render() directive"), t.line, t.col);
            }
            render = parseRenderDirective();
            hasRender = true;
            while (peek().type == TokenType::SEMICOLON) advance();
            if (!leadingComments.isEmpty()) {
                render.insert(QStringLiteral("leadingComments"), leadingComments);
            }
            const QJsonArray trailing = collectComments();
            for (const QJsonValue& c : trailing) trailingComments.append(c);
            break;
        }
        QJsonObject stmt = parseStatement();
        if (!leadingComments.isEmpty()) {
            stmt.insert(QStringLiteral("leadingComments"), leadingComments);
        }
        if (stmt.value(QStringLiteral("type")).toString() == NodeKind::VarAssign) {
            vars.append(stmt);
        } else {
            plans.append(stmt);
        }
        while (peek().type == TokenType::SEMICOLON) advance();
    }
    expect(TokenType::EOF_, QStringLiteral("Expected end of input"));
    if (!hasSearch_ || searchOrder_.isEmpty()) {
        throw DslSyntaxError(QStringLiteral(
            "Missing required 'search' directive. Every program must start with 'search <namespace>, ...' "
            "to specify namespace search order."));
    }

    QJsonObject program;
    program.insert(QStringLiteral("type"), NodeKind::Program);
    program.insert(QStringLiteral("plans"), plans);
    program.insert(QStringLiteral("render"), hasRender ? QJsonValue(render) : QJsonValue(QJsonValue::Null));
    if (!vars.isEmpty()) program.insert(QStringLiteral("vars"), vars);
    if (!trailingComments.isEmpty()) program.insert(QStringLiteral("trailingComments"), trailingComments);

    QJsonArray searchOrderJson;
    for (const QString& ns : searchOrder_) searchOrderJson.append(ns);
    QJsonObject namespaceMeta;
    namespaceMeta.insert(QStringLiteral("imports"), namespaceImports_);
    namespaceMeta.insert(QStringLiteral("default"),
                          hasNamespaceDefault_ ? QJsonValue(namespaceDefault_) : QJsonValue(QJsonValue::Null));
    namespaceMeta.insert(QStringLiteral("searchOrder"), searchOrderJson);
    program.insert(QStringLiteral("namespace"), namespaceMeta);
    return program;
}

void Parser::parseSearchDirective() {
    if (hasSearch_) {
        const Token t = peek();
        throw DslSyntaxError::at(QStringLiteral("Only one search directive is allowed per program"), t.line, t.col);
    }
    advance(); // consume 'search'
    QStringList namespaces;

    const Token first = peek();
    if (!namespaceTokenTypes().contains(first.type)) {
        throw DslSyntaxError::at(QStringLiteral("Expected namespace identifier after search"), first.line, first.col);
    }
    advance();
    validateNamespace(first);
    namespaces.append(first.lexeme);

    while (peek().type == TokenType::COMMA) {
        advance();
        const Token nsToken = peek();
        if (!namespaceTokenTypes().contains(nsToken.type)) {
            throw DslSyntaxError::at(QStringLiteral("Expected namespace identifier after comma"), nsToken.line,
                                      nsToken.col);
        }
        advance();
        validateNamespace(nsToken);
        namespaces.append(nsToken.lexeme);
    }

    hasSearch_ = true;
    searchOrder_ = namespaces;

    namespaceImports_ = QJsonArray();
    for (const QString& nm : namespaces) {
        QJsonObject imp;
        imp.insert(QStringLiteral("name"), nm);
        imp.insert(QStringLiteral("source"), QStringLiteral("search"));
        imp.insert(QStringLiteral("explicit"), true);
        namespaceImports_.append(imp);
    }
    namespaceDefault_ = QJsonObject();
    namespaceDefault_.insert(QStringLiteral("name"), namespaces.first());
    namespaceDefault_.insert(QStringLiteral("source"), QStringLiteral("search"));
    namespaceDefault_.insert(QStringLiteral("explicit"), true);
    hasNamespaceDefault_ = true;

    while (peek().type == TokenType::SEMICOLON) advance();
}

void Parser::validateNamespace(const Token& token) {
    if (!isValidNamespaceName(token.lexeme)) {
        throw DslSyntaxError::at(QStringLiteral("Invalid namespace '%1'. Valid namespaces: %2")
                                      .arg(token.lexeme, validNamespaces().join(QStringLiteral(", "))),
                                  token.line, token.col);
    }
}

QJsonArray Parser::parseBlock() {
    expect(TokenType::LBRACE, QStringLiteral("Expect '{'"));
    QJsonArray body;
    while (peek().type != TokenType::RBRACE) {
        body.append(parseStatement());
        while (peek().type == TokenType::SEMICOLON) advance();
    }
    expect(TokenType::RBRACE, QStringLiteral("Expect '}'"));
    return body;
}

QJsonObject Parser::parseStatement() {
    if (peek().type == TokenType::SEARCH) {
        const Token t = peek();
        throw DslSyntaxError::at(QStringLiteral("'search' directive is only allowed at the start of the program"),
                                  t.line, t.col);
    }
    if (peek().type == TokenType::LET) {
        advance();
        const QString name = expect(TokenType::IDENT, QStringLiteral("Expected identifier")).lexeme;
        expect(TokenType::EQUAL, QStringLiteral("Expect '='"));
        if (!exprStartTokens().contains(peek().type)) {
            const Token t = peek();
            throw DslSyntaxError::at(QStringLiteral("Expected expression after '='"), t.line, t.col);
        }
        const QJsonObject expr = parseAdditive();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::VarAssign);
        node.insert(QStringLiteral("name"), name);
        node.insert(QStringLiteral("expr"), expr);
        return node;
    }

    const QString tt = peek().type;
    if (tt == TokenType::IF) {
        advance();
        expect(TokenType::LPAREN, QStringLiteral("Expect '('"));
        const QJsonObject condition = parseAdditive();
        expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));
        const QJsonArray thenBlock = parseBlock();
        QJsonArray elifList;
        while (peek().type == TokenType::ELIF) {
            advance();
            expect(TokenType::LPAREN, QStringLiteral("Expect '('"));
            const QJsonObject ec = parseAdditive();
            expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));
            const QJsonArray body = parseBlock();
            QJsonObject elifEntry;
            elifEntry.insert(QStringLiteral("condition"), ec);
            elifEntry.insert(QStringLiteral("then"), body);
            elifList.append(elifEntry);
        }
        QJsonValue elseBranch = QJsonValue(QJsonValue::Null);
        if (peek().type == TokenType::ELSE) {
            advance();
            elseBranch = parseBlock();
        }
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::IfStmt);
        node.insert(QStringLiteral("condition"), condition);
        node.insert(QStringLiteral("then"), thenBlock);
        node.insert(QStringLiteral("elif"), elifList);
        node.insert(QStringLiteral("else"), elseBranch);
        return node;
    }
    if (tt == TokenType::BREAK) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Break);
        return node;
    }
    if (tt == TokenType::CONTINUE) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Continue);
        return node;
    }
    if (tt == TokenType::RETURN) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Return);
        if (exprStartTokens().contains(peek().type)) {
            node.insert(QStringLiteral("value"), parseAdditive());
        }
        return node;
    }

    const QJsonArray chain = parseChain(QStringLiteral("statement"));
    // Extract write/write3d only if the chain TERMINATES with a
    // Write/Write3D node -- mid-chain writes don't count as terminal.
    QJsonValue write = QJsonValue(QJsonValue::Null);
    QJsonValue write3d = QJsonValue(QJsonValue::Null);
    if (!chain.isEmpty()) {
        const QJsonObject lastNode = chain.last().toObject();
        const QString lastType = lastNode.value(QStringLiteral("type")).toString();
        if (lastType == NodeKind::Write) {
            write = lastNode.value(QStringLiteral("surface"));
        } else if (lastType == NodeKind::Write3D) {
            QJsonObject w3;
            w3.insert(QStringLiteral("tex3d"), lastNode.value(QStringLiteral("tex3d")));
            w3.insert(QStringLiteral("geo"), lastNode.value(QStringLiteral("geo")));
            write3d = w3;
        }
    }
    // NOTE: this wrapper deliberately has NO "type" key (identified by
    // the "chain" key alone) -- matches the reference exactly.
    QJsonObject stmt;
    stmt.insert(QStringLiteral("chain"), chain);
    stmt.insert(QStringLiteral("write"), write);
    stmt.insert(QStringLiteral("write3d"), write3d);
    return stmt;
}

QJsonArray Parser::parseChain(const QString& context) {
    const QJsonObject firstCall = parseCall();
    QJsonArray calls;
    calls.append(firstCall);
    while (true) {
        // Comments can appear before the DOT in a chain (e.g. `noise() \n
        // // comment \n .bloom()`); if there is no DOT after them, restore
        // position so they belong to whatever follows this chain instead.
        const int savedPos = current_;
        const QJsonArray leadingComments = collectComments();
        if (peek().type != TokenType::DOT) {
            current_ = savedPos;
            break;
        }
        advance(); // consume '.'
        const QJsonArray postDotComments = collectComments();
        QJsonArray allComments;
        for (const QJsonValue& c : leadingComments) allComments.append(c);
        for (const QJsonValue& c : postDotComments) allComments.append(c);

        const QString nextType = peek().type;
        if (nextType == TokenType::WRITE || nextType == TokenType::WRITE3D) {
            if (context == QStringLiteral("expression")) {
                const Token t = peek();
                throw DslSyntaxError::at(QStringLiteral("'.write()' is only allowed in statement context"), t.line,
                                          t.col);
            }
            QJsonObject writeNode = parseWriteCall();
            if (!allComments.isEmpty()) writeNode.insert(QStringLiteral("leadingComments"), allComments);
            calls.append(writeNode);
            continue;
        }
        if (nextType == TokenType::SUBCHAIN) {
            QJsonObject subchainNode = parseSubchainCall();
            if (!allComments.isEmpty()) subchainNode.insert(QStringLiteral("leadingComments"), allComments);
            calls.append(subchainNode);
            continue;
        }
        QJsonObject call = parseCall();
        if (!allComments.isEmpty()) call.insert(QStringLiteral("leadingComments"), allComments);
        calls.append(call);
    }
    return calls;
}

QJsonObject Parser::parseWriteCall() {
    const Token tok = peek();
    const QString tokenType = tok.type;
    const int tokenLine = tok.line;
    const int tokenCol = tok.col;

    if (tokenType == TokenType::WRITE) {
        advance(); // consume 'write'
        expect(TokenType::LPAREN, QStringLiteral("Expect '('"));
        QJsonObject surface;
        const QString pt = peek().type;
        if (pt == TokenType::OUTPUT_REF) {
            surface = refNode(NodeKind::OutputRef, advance().lexeme);
        } else if (pt == TokenType::XYZ_REF) {
            surface = refNode(NodeKind::XyzRef, advance().lexeme);
        } else if (pt == TokenType::VEL_REF) {
            surface = refNode(NodeKind::VelRef, advance().lexeme);
        } else if (pt == TokenType::RGBA_REF) {
            surface = refNode(NodeKind::RgbaRef, advance().lexeme);
        } else if (pt == TokenType::MESH_REF) {
            surface = refNode(NodeKind::MeshRef, advance().lexeme);
        } else if (pt == TokenType::IDENT && peek().lexeme == QStringLiteral("none")) {
            // "none" is a valid target meaning "don't write to any surface".
            surface = refNode(NodeKind::OutputRef, advance().lexeme);
        } else {
            const Token p = peek();
            throw DslSyntaxError::at(
                QStringLiteral(
                    "write() requires an explicit surface reference (e.g., o0, o1, xyz0, vel0, rgba0, mesh0, none)"),
                p.line, p.col);
        }
        expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Write);
        node.insert(QStringLiteral("surface"), surface);
        node.insert(QStringLiteral("loc"), ast::loc(tokenLine, tokenCol));
        return node;
    }
    if (tokenType == TokenType::WRITE3D) {
        advance(); // consume 'write3d'
        expect(TokenType::LPAREN, QStringLiteral("Expect '('"));
        QJsonObject tex3d;
        QString pt = peek().type;
        if (pt == TokenType::IDENT || pt == TokenType::OUTPUT_REF || pt == TokenType::VOL_REF) {
            if (pt == TokenType::OUTPUT_REF) {
                tex3d = refNode(NodeKind::OutputRef, advance().lexeme);
            } else if (pt == TokenType::VOL_REF) {
                tex3d = refNode(NodeKind::VolRef, advance().lexeme);
            } else {
                tex3d = refNode(NodeKind::Ident, advance().lexeme);
            }
        } else {
            const Token p = peek();
            throw DslSyntaxError::at(QStringLiteral("Expected tex3d reference in write3d()"), p.line, p.col);
        }
        expect(TokenType::COMMA, QStringLiteral("Expect ',' between tex3d and geo in write3d()"));
        QJsonObject geo;
        pt = peek().type;
        if (pt == TokenType::IDENT || pt == TokenType::OUTPUT_REF || pt == TokenType::GEO_REF) {
            if (pt == TokenType::OUTPUT_REF) {
                geo = refNode(NodeKind::OutputRef, advance().lexeme);
            } else if (pt == TokenType::GEO_REF) {
                geo = refNode(NodeKind::GeoRef, advance().lexeme);
            } else {
                geo = refNode(NodeKind::Ident, advance().lexeme);
            }
        } else {
            const Token p = peek();
            throw DslSyntaxError::at(QStringLiteral("Expected geo reference in write3d()"), p.line, p.col);
        }
        expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Write3D);
        node.insert(QStringLiteral("tex3d"), tex3d);
        node.insert(QStringLiteral("geo"), geo);
        node.insert(QStringLiteral("loc"), ast::loc(tokenLine, tokenCol));
        return node;
    }
    throw DslSyntaxError::at(QStringLiteral("Expected write or write3d"), tokenLine, tokenCol);
}

QJsonObject Parser::parseSubchainCall() {
    const int tokenLine = peek().line;
    const int tokenCol = peek().col;

    advance(); // consume 'subchain'
    expect(TokenType::LPAREN, QStringLiteral("Expect '(' after subchain"));

    // key -> {type:'String', value:...}; ANY identifier key is syntactically
    // accepted here (matches the reference), but only "name"/"id" are ever
    // read back below -- everything else is silently discarded.
    QJsonObject kwargs;
    if (peek().type != TokenType::RPAREN) {
        if (peek().type == TokenType::STRING) {
            // Positional name: subchain("name"). NOTE this is an if/else-if
            // with the keyword-loop branch below -- a positional string
            // can NEVER be followed by more args (e.g. `subchain("n", id:
            // "x")` is a syntax error, not "name + id"): only one of the
            // two branches ever runs.
            QJsonObject nameVal;
            nameVal.insert(QStringLiteral("type"), NodeKind::String);
            nameVal.insert(QStringLiteral("value"), advance().lexeme);
            kwargs.insert(QStringLiteral("name"), nameVal);
        } else if (peek().type == TokenType::IDENT && typeAt(current_ + 1) == TokenType::COLON) {
            // Keyword arguments: subchain(name: "...", id: "...")
            while (peek().type == TokenType::IDENT && typeAt(current_ + 1) == TokenType::COLON) {
                const QString key = advance().lexeme;
                advance(); // consume ':'
                if (peek().type != TokenType::STRING) {
                    const Token p = peek();
                    throw DslSyntaxError::at(QStringLiteral("Expected string value for subchain %1").arg(key),
                                              p.line, p.col);
                }
                QJsonObject val;
                val.insert(QStringLiteral("type"), NodeKind::String);
                val.insert(QStringLiteral("value"), advance().lexeme);
                kwargs.insert(key, val);
                if (peek().type == TokenType::COMMA) advance();
            }
        }
    }
    expect(TokenType::RPAREN, QStringLiteral("Expect ')' after subchain arguments"));
    expect(TokenType::LBRACE, QStringLiteral("Expect '{' to start subchain body"));

    QJsonArray body;
    while (peek().type != TokenType::RBRACE) {
        const QJsonArray leadingComments = collectComments();
        if (peek().type == TokenType::RBRACE) break;
        if (peek().type != TokenType::DOT) {
            const Token p = peek();
            throw DslSyntaxError::at(QStringLiteral("Expected '.' before chain element in subchain body"), p.line,
                                      p.col);
        }
        advance(); // consume '.'
        const QJsonArray postDotComments = collectComments();
        QJsonArray allComments;
        for (const QJsonValue& c : leadingComments) allComments.append(c);
        for (const QJsonValue& c : postDotComments) allComments.append(c);
        QJsonObject call = parseCall();
        if (!allComments.isEmpty()) call.insert(QStringLiteral("leadingComments"), allComments);
        body.append(call);
    }
    expect(TokenType::RBRACE, QStringLiteral("Expect '}' to end subchain body"));

    if (body.isEmpty()) {
        throw DslSyntaxError::at(QStringLiteral("Subchain body cannot be empty"), tokenLine, tokenCol);
    }

    // Reference: `kwargs.name?.value || null` -- a FALSY-OR, so an
    // explicitly empty-string value ALSO becomes null, not "". Ported
    // verbatim (see file header note); do not "fix" this.
    auto resolveFalsyStringOrNull = [&](const QString& key) -> QJsonValue {
        if (!kwargs.contains(key)) return QJsonValue(QJsonValue::Null);
        const QString value = kwargs.value(key).toObject().value(QStringLiteral("value")).toString();
        return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
    };

    QJsonObject node;
    node.insert(QStringLiteral("type"), NodeKind::Subchain);
    node.insert(QStringLiteral("name"), resolveFalsyStringOrNull(QStringLiteral("name")));
    node.insert(QStringLiteral("id"), resolveFalsyStringOrNull(QStringLiteral("id")));
    node.insert(QStringLiteral("body"), body);
    node.insert(QStringLiteral("loc"), ast::loc(tokenLine, tokenCol));
    return node;
}

QJsonObject Parser::parseCall() {
    const Token nameToken = expect(TokenType::IDENT, QStringLiteral("Expected identifier"));
    // Inline namespace syntax (e.g., nd.noise()) is forbidden. This ONLY
    // fires for exactly one dot segment immediately followed by a call;
    // two-or-more-segment dotted calls fall through to the unconditional
    // expect(LPAREN) below and hit the generic "Expect '('" instead (a
    // reference bug-for-bug -- see file header note).
    if (peek().type == TokenType::DOT) {
        const Token* next = tokenAt(current_ + 1);
        if (next && next->type == TokenType::IDENT) {
            const Token* after = tokenAt(current_ + 2);
            if (after && after->type == TokenType::LPAREN) {
                throw DslSyntaxError::at(
                    QStringLiteral("Inline namespace syntax '%1.%2()' is not allowed. Use 'search %1' at the start "
                                   "of the program instead,")
                        .arg(nameToken.lexeme, next->lexeme),
                    nameToken.line, nameToken.col);
            }
        }
    }
    expect(TokenType::LPAREN, QStringLiteral("Expect '('"));
    QJsonArray args;
    QJsonObject kwargs;
    QStringList kwargOrder;
    bool keyword = false;
    bool positional = false;
    const bool allowMixed = nameToken.lexeme == QStringLiteral("midi")
        || nameToken.lexeme == QStringLiteral("audio");
    if (peek().type != TokenType::RPAREN) {
        while (true) {
            if (peek().type == TokenType::IDENT && typeAt(current_ + 1) == TokenType::COLON) {
                if (positional && !allowMixed) {
                    const Token t = peek();
                    throw DslSyntaxError::at(QStringLiteral("Cannot mix positional and keyword arguments"), t.line,
                                              t.col);
                }
                keyword = true;
                const QString kwargName = peek().lexeme;
                parseKwarg(kwargs);
                kwargOrder.append(kwargName);
            } else {
                if (keyword && !allowMixed) {
                    const Token t = peek();
                    throw DslSyntaxError::at(QStringLiteral("Cannot mix positional and keyword arguments"), t.line,
                                              t.col);
                }
                positional = true;
                args.append(parseArg());
            }
            if (peek().type != TokenType::COMMA) break;
            advance();
            if (peek().type == TokenType::RPAREN) break;
        }
    }
    expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));

    QJsonObject call;
    call.insert(QStringLiteral("type"), NodeKind::Call);
    call.insert(QStringLiteral("name"), nameToken.lexeme);
    call.insert(QStringLiteral("args"), args);
    if (keyword) call.insert(QStringLiteral("kwargs"), kwargs);

    const QString lexeme = nameToken.lexeme;
    if (lexeme == QStringLiteral("from")) {
        return transformFromInvocation(call, nameToken);
    }
    // osc() as a value oscillator (not the synth.osc generator effect) --
    // 4-way heuristic, checked in this order.
    if (lexeme == QStringLiteral("osc")) {
        static const QSet<QString> oscKwargKeys = {
            QStringLiteral("type"), QStringLiteral("min"),    QStringLiteral("max"),
            QStringLiteral("speed"), QStringLiteral("offset"), QStringLiteral("seed"),
        };
        const bool hasTypeKwarg = kwargs.contains(QStringLiteral("type"));
        const bool firstArgIsOscKind = !args.isEmpty()
            && args.at(0).toObject().value(QStringLiteral("type")).toString() == NodeKind::Member
            && !args.at(0).toObject().value(QStringLiteral("path")).toArray().isEmpty()
            && args.at(0).toObject().value(QStringLiteral("path")).toArray().at(0).toString()
                   == QStringLiteral("oscKind");
        const bool isBareOsc = args.isEmpty() && kwargs.isEmpty();
        bool hasOnlyOscKwargs = !kwargs.isEmpty();
        if (hasOnlyOscKwargs) {
            for (const QString& k : kwargs.keys()) {
                if (!oscKwargKeys.contains(k)) {
                    hasOnlyOscKwargs = false;
                    break;
                }
            }
        }
        if (hasTypeKwarg || firstArgIsOscKind || isBareOsc || hasOnlyOscKwargs) {
            return transformOscInvocation(call, nameToken);
        }
        // else fall through to return as a regular Call node for the
        // synth effect (e.g. a positional non-oscKind arg with no kwargs).
    }
    if (lexeme == QStringLiteral("midi")) {
        return transformMidiInvocation(call, nameToken, kwargOrder);
    }
    if (lexeme == QStringLiteral("audio")) {
        return transformAudioInvocation(call, nameToken, kwargOrder);
    }
    // read()/read3d() are pipeline built-ins. NOTE: `surface`/`tex3d` are
    // OMITTED entirely (not null) when unresolved -- the reference has no
    // `|| null` fallback for them, unlike `geo` (see file header note).
    if (lexeme == QStringLiteral("read")) {
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Read);
        if (!args.isEmpty()) {
            node.insert(QStringLiteral("surface"), args.at(0));
        } else if (kwargs.contains(QStringLiteral("tex"))) {
            node.insert(QStringLiteral("surface"), kwargs.value(QStringLiteral("tex")));
        } else if (kwargs.contains(QStringLiteral("surface"))) {
            node.insert(QStringLiteral("surface"), kwargs.value(QStringLiteral("surface")));
        }
        node.insert(QStringLiteral("loc"), ast::loc(nameToken.line, nameToken.col));
        const QJsonObject skip = kwargs.value(QStringLiteral("_skip")).toObject();
        if (skip.value(QStringLiteral("type")).toString() == NodeKind::Boolean
            && skip.value(QStringLiteral("value")).toBool()) {
            node.insert(QStringLiteral("_skip"), true);
        }
        return node;
    }
    if (lexeme == QStringLiteral("read3d")) {
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Read3D);
        if (!args.isEmpty()) {
            node.insert(QStringLiteral("tex3d"), args.at(0));
        } else if (kwargs.contains(QStringLiteral("tex3d"))) {
            node.insert(QStringLiteral("tex3d"), kwargs.value(QStringLiteral("tex3d")));
        }
        QJsonValue geo;
        if (args.size() > 1) {
            geo = args.at(1);
        } else if (kwargs.contains(QStringLiteral("geo"))) {
            geo = kwargs.value(QStringLiteral("geo"));
        }
        node.insert(QStringLiteral("geo"), geo.isUndefined() ? QJsonValue(QJsonValue::Null) : geo);
        node.insert(QStringLiteral("loc"), ast::loc(nameToken.line, nameToken.col));
        const QJsonObject skip = kwargs.value(QStringLiteral("_skip")).toObject();
        if (skip.value(QStringLiteral("type")).toString() == NodeKind::Boolean
            && skip.value(QStringLiteral("value")).toBool()) {
            node.insert(QStringLiteral("_skip"), true);
        }
        return node;
    }
    return call;
}

QJsonObject Parser::transformOscInvocation(const QJsonObject& call, const Token& nameToken) {
    const QJsonArray args = call.value(QStringLiteral("args")).toArray();
    const QJsonObject kwargs = call.value(QStringLiteral("kwargs")).toObject();
    static const QStringList paramOrder = {
        QStringLiteral("type"), QStringLiteral("min"),    QStringLiteral("max"),
        QStringLiteral("speed"), QStringLiteral("offset"), QStringLiteral("seed"),
    };

    for (const QString& key : kwargs.keys()) {
        if (!paramOrder.contains(key)) {
            // NOTE: location is embedded MID-message here, followed by
            // "Valid: ..." -- does not fit the DslSyntaxError::at()
            // trailing-suffix shape, so built directly (matches the
            // reference's template literal exactly).
            throw DslSyntaxError(QStringLiteral("osc() unknown parameter '%1' at line %2 col %3. Valid: %4")
                                      .arg(key)
                                      .arg(nameToken.line)
                                      .arg(nameToken.col)
                                      .arg(paramOrder.join(QStringLiteral(", "))),
                                  nameToken.line, nameToken.col);
        }
    }

    auto resolve = [&](const QString& name, int index, const QJsonValue& dflt) -> QJsonValue {
        if (kwargs.contains(name)) return kwargs.value(name);
        if (index < args.size()) return args.at(index);
        return dflt;
    };

    QJsonObject node;
    node.insert(QStringLiteral("type"), NodeKind::Oscillator);
    node.insert(QStringLiteral("oscType"),
                resolve(QStringLiteral("type"), 0, ast::memberOf(QStringLiteral("oscKind"), QStringLiteral("sine"))));
    node.insert(QStringLiteral("min"), resolve(QStringLiteral("min"), 1, ast::number(0)));
    node.insert(QStringLiteral("max"), resolve(QStringLiteral("max"), 2, ast::number(1)));
    node.insert(QStringLiteral("speed"), resolve(QStringLiteral("speed"), 3, ast::number(1)));
    node.insert(QStringLiteral("offset"), resolve(QStringLiteral("offset"), 4, ast::number(0)));
    node.insert(QStringLiteral("seed"), resolve(QStringLiteral("seed"), 5, ast::number(1)));
    node.insert(QStringLiteral("loc"), ast::loc(nameToken.line, nameToken.col));
    return node;
}

QJsonObject Parser::transformMidiInvocation(const QJsonObject& call, const Token& nameToken,
                                            const QStringList& kwargOrder) {
    const QJsonArray args = call.value(QStringLiteral("args")).toArray();
    const QJsonObject kwargs = call.value(QStringLiteral("kwargs")).toObject();

    static const QStringList paramOrder = {
        QStringLiteral("channel"), QStringLiteral("mode"), QStringLiteral("min"),
        QStringLiteral("max"), QStringLiteral("sensitivity"),
    };
    static const QStringList keywordOnlyParams = {
        QStringLiteral("name"), QStringLiteral("id"),
    };
    QStringList validParams = paramOrder;
    validParams.append(keywordOnlyParams);
    if (args.size() > paramOrder.size()) {
        throw DslSyntaxError::at(QStringLiteral("midi() name and id are keyword-only"), nameToken.line,
                                  nameToken.col);
    }
    for (const QString& key : kwargOrder) {
        if (!validParams.contains(key)) {
            throw DslSyntaxError(QStringLiteral("midi() unknown parameter '%1' at line %2 col %3. Valid: %4")
                                      .arg(key)
                                      .arg(nameToken.line)
                                      .arg(nameToken.col)
                                      .arg(validParams.join(QStringLiteral(", "))),
                                  nameToken.line, nameToken.col);
        }
    }

    const QJsonObject defaults = {
        {QStringLiteral("mode"), ast::memberOf(QStringLiteral("midiMode"), QStringLiteral("velocity"))},
        {QStringLiteral("min"), ast::number(0)},
        {QStringLiteral("max"), ast::number(1)},
        {QStringLiteral("sensitivity"), ast::number(1)},
    };
    QJsonObject resolved;
    int posCursor = 0;
    for (const QString& paramName : paramOrder) {
        if (kwargs.contains(paramName)) {
            resolved.insert(paramName, kwargs.value(paramName));
        } else if (posCursor < args.size()) {
            resolved.insert(paramName, args.at(posCursor++));
        } else if (defaults.contains(paramName)) {
            resolved.insert(paramName, defaults.value(paramName));
        }
    }
    if (posCursor < args.size()) {
        throw DslSyntaxError::at(QStringLiteral("midi() has an excess positional argument"), nameToken.line,
                                  nameToken.col);
    }

    const QJsonValue channel = resolved.value(QStringLiteral("channel"));
    if (channel.isUndefined()) {
        throw DslSyntaxError::at(QStringLiteral("midi() requires 'channel' argument"), nameToken.line,
                                  nameToken.col);
    }
    if (kwargs.contains(QStringLiteral("id")) && !kwargs.contains(QStringLiteral("name"))) {
        throw DslSyntaxError::at(QStringLiteral("midi() 'id' requires readable 'name'"), nameToken.line,
                                  nameToken.col);
    }
    for (const QString& paramName : keywordOnlyParams) {
        if (!kwargs.contains(paramName)) continue;
        const QJsonObject value = kwargs.value(paramName).toObject();
        if (value.value(QStringLiteral("type")).toString() != NodeKind::String) {
            throw DslSyntaxError::at(QStringLiteral("midi() '%1' requires a quoted string").arg(paramName),
                                      nameToken.line, nameToken.col);
        }
        if (value.value(QStringLiteral("value")).toString().isEmpty()) {
            throw DslSyntaxError::at(QStringLiteral("midi() '%1' must not be empty").arg(paramName),
                                      nameToken.line, nameToken.col);
        }
    }

    QJsonObject node;
    node.insert(QStringLiteral("type"), NodeKind::Midi);
    node.insert(QStringLiteral("channel"), channel);
    node.insert(QStringLiteral("mode"), resolved.value(QStringLiteral("mode")));
    node.insert(QStringLiteral("min"), resolved.value(QStringLiteral("min")));
    node.insert(QStringLiteral("max"), resolved.value(QStringLiteral("max")));
    node.insert(QStringLiteral("sensitivity"), resolved.value(QStringLiteral("sensitivity")));
    if (kwargs.contains(QStringLiteral("name"))) {
        node.insert(QStringLiteral("name"), kwargs.value(QStringLiteral("name")));
    }
    if (kwargs.contains(QStringLiteral("id"))) node.insert(QStringLiteral("id"), kwargs.value(QStringLiteral("id")));
    node.insert(QStringLiteral("loc"), ast::loc(nameToken.line, nameToken.col));
    return node;
}

QJsonObject Parser::transformAudioInvocation(const QJsonObject& call, const Token& nameToken,
                                             const QStringList& kwargOrder) {
    const QJsonArray args = call.value(QStringLiteral("args")).toArray();
    const QJsonObject kwargs = call.value(QStringLiteral("kwargs")).toObject();

    static const QStringList paramOrder = {
        QStringLiteral("band"), QStringLiteral("min"), QStringLiteral("max"),
    };
    static const QStringList keywordOnlyParams = {
        QStringLiteral("channel"), QStringLiteral("name"), QStringLiteral("id"),
    };
    QStringList validParams = paramOrder;
    validParams.append(keywordOnlyParams);
    if (args.size() > paramOrder.size()) {
        throw DslSyntaxError::at(QStringLiteral("audio() channel, name and id are keyword-only"), nameToken.line,
                                  nameToken.col);
    }
    for (const QString& key : kwargOrder) {
        if (!validParams.contains(key)) {
            throw DslSyntaxError(QStringLiteral("audio() unknown parameter '%1' at line %2 col %3. Valid: %4")
                                      .arg(key)
                                      .arg(nameToken.line)
                                      .arg(nameToken.col)
                                      .arg(validParams.join(QStringLiteral(", "))),
                                  nameToken.line, nameToken.col);
        }
    }

    const QJsonObject defaults = {
        {QStringLiteral("min"), ast::number(0)},
        {QStringLiteral("max"), ast::number(1)},
    };
    QJsonObject resolved;
    int posCursor = 0;
    for (const QString& paramName : paramOrder) {
        if (kwargs.contains(paramName)) {
            resolved.insert(paramName, kwargs.value(paramName));
        } else if (posCursor < args.size()) {
            resolved.insert(paramName, args.at(posCursor++));
        } else if (defaults.contains(paramName)) {
            resolved.insert(paramName, defaults.value(paramName));
        }
    }
    if (posCursor < args.size()) {
        throw DslSyntaxError::at(QStringLiteral("audio() has an excess positional argument"), nameToken.line,
                                  nameToken.col);
    }

    const QJsonValue band = resolved.value(QStringLiteral("band"));
    if (band.isUndefined()) {
        throw DslSyntaxError::at(QStringLiteral("audio() requires 'band' argument"), nameToken.line, nameToken.col);
    }
    if (kwargs.contains(QStringLiteral("id")) && !kwargs.contains(QStringLiteral("name"))) {
        throw DslSyntaxError::at(QStringLiteral("audio() 'id' requires readable 'name'"), nameToken.line,
                                  nameToken.col);
    }
    if (kwargs.contains(QStringLiteral("channel")) != kwargs.contains(QStringLiteral("name"))) {
        throw DslSyntaxError::at(QStringLiteral("audio() selected device requires both 'name' and 'channel'"),
                                  nameToken.line, nameToken.col);
    }
    for (const QString& paramName : {QStringLiteral("name"), QStringLiteral("id")}) {
        if (!kwargs.contains(paramName)) continue;
        const QJsonObject value = kwargs.value(paramName).toObject();
        if (value.value(QStringLiteral("type")).toString() != NodeKind::String) {
            throw DslSyntaxError::at(QStringLiteral("audio() '%1' requires a quoted string").arg(paramName),
                                      nameToken.line, nameToken.col);
        }
        if (value.value(QStringLiteral("value")).toString().isEmpty()) {
            throw DslSyntaxError::at(QStringLiteral("audio() '%1' must not be empty").arg(paramName),
                                      nameToken.line, nameToken.col);
        }
    }

    QJsonObject node;
    node.insert(QStringLiteral("type"), NodeKind::Audio);
    node.insert(QStringLiteral("band"), band);
    node.insert(QStringLiteral("min"), resolved.value(QStringLiteral("min")));
    node.insert(QStringLiteral("max"), resolved.value(QStringLiteral("max")));
    if (kwargs.contains(QStringLiteral("channel"))) {
        node.insert(QStringLiteral("channel"), kwargs.value(QStringLiteral("channel")));
    }
    if (kwargs.contains(QStringLiteral("name"))) {
        node.insert(QStringLiteral("name"), kwargs.value(QStringLiteral("name")));
    }
    if (kwargs.contains(QStringLiteral("id"))) node.insert(QStringLiteral("id"), kwargs.value(QStringLiteral("id")));
    node.insert(QStringLiteral("loc"), ast::loc(nameToken.line, nameToken.col));
    return node;
}

QJsonObject Parser::transformFromInvocation(const QJsonObject& call, const Token& nameToken) {
    auto fail = [&](const QString& message) { throw DslSyntaxError::at(message, nameToken.line, nameToken.col); };

    const QJsonObject kwargs = call.value(QStringLiteral("kwargs")).toObject();
    if (!kwargs.isEmpty()) {
        fail(QStringLiteral("'from' does not support named arguments"));
    }
    const QJsonArray args = call.value(QStringLiteral("args")).toArray();
    if (args.size() != 2) {
        fail(QStringLiteral("'from' requires exactly two arguments (namespace, call)"));
    }
    const QJsonObject namespaceArg = args.at(0).toObject();
    const QJsonObject targetArg = args.at(1).toObject();
    const QString namespaceArgType = namespaceArg.value(QStringLiteral("type")).toString();
    if (namespaceArgType != NodeKind::Ident && namespaceArgType != NodeKind::Member) {
        fail(QStringLiteral("'from' namespace argument must be an identifier"));
    }
    QString namespaceName;
    if (namespaceArgType == NodeKind::Member) {
        QStringList segs;
        for (const QJsonValue& v : namespaceArg.value(QStringLiteral("path")).toArray()) segs.append(v.toString());
        namespaceName = segs.join(QLatin1Char('.'));
    } else {
        namespaceName = namespaceArg.value(QStringLiteral("name")).toString();
    }
    if (namespaceName.isEmpty()) {
        fail(QStringLiteral("'from' namespace argument must be non-empty"));
    }

    QJsonObject targetCall;
    bool haveTargetCall = false;
    const QString targetType = targetArg.value(QStringLiteral("type")).toString();
    if (targetType == NodeKind::Call) {
        targetCall = targetArg;
        haveTargetCall = true;
    } else if (targetType == NodeKind::Chain) {
        // Unreachable in practice: parsePrimary always unwraps a
        // length-1 chain to its single element directly, so a Chain node
        // here can only ever have 2+ elements. Ported anyway for fidelity
        // with the reference's own defensive check.
        const QJsonArray innerChain = targetArg.value(QStringLiteral("chain")).toArray();
        if (innerChain.size() == 1) {
            const QJsonObject head = innerChain.at(0).toObject();
            if (head.value(QStringLiteral("type")).toString() == NodeKind::Call) {
                targetCall = head;
                haveTargetCall = true;
            }
        }
    }
    if (!haveTargetCall) {
        fail(QStringLiteral("'from' second argument must be a call expression"));
    }

    QJsonObject replacement;
    replacement.insert(QStringLiteral("type"), NodeKind::Call);
    replacement.insert(QStringLiteral("name"), targetCall.value(QStringLiteral("name")));
    replacement.insert(QStringLiteral("args"), targetCall.value(QStringLiteral("args")).toArray());
    if (targetCall.contains(QStringLiteral("kwargs"))) {
        replacement.insert(QStringLiteral("kwargs"), targetCall.value(QStringLiteral("kwargs")).toObject());
    }
    QJsonObject overrideNamespace;
    overrideNamespace.insert(QStringLiteral("name"), namespaceName);
    QJsonArray pathArr;
    pathArr.append(namespaceName);
    overrideNamespace.insert(QStringLiteral("path"), pathArr);
    overrideNamespace.insert(QStringLiteral("explicit"), true);
    overrideNamespace.insert(QStringLiteral("source"), QStringLiteral("from"));
    overrideNamespace.insert(QStringLiteral("resolved"), namespaceName);
    QJsonArray searchOrderArr;
    searchOrderArr.append(namespaceName);
    overrideNamespace.insert(QStringLiteral("searchOrder"), searchOrderArr);
    overrideNamespace.insert(QStringLiteral("fromOverride"), true);
    replacement.insert(QStringLiteral("namespace"), overrideNamespace);
    return replacement;
}

void Parser::parseKwarg(QJsonObject& obj) {
    const QString key = expect(TokenType::IDENT, QStringLiteral("Expected identifier")).lexeme;
    expect(TokenType::COLON, QStringLiteral("Expect ':'"));
    if (!exprStartTokens().contains(peek().type)) {
        // NOTE: reference message says "after '='" even though a kwarg
        // uses ':' -- a copy-paste artifact in the source of truth, kept
        // verbatim (never "clean up" a reference wording quirk).
        const Token t = peek();
        throw DslSyntaxError::at(QStringLiteral("Expected expression after '='"), t.line, t.col);
    }
    obj.insert(key, parseArg());
}

QJsonObject Parser::parseAdditive() {
    QJsonObject node = parseMultiplicative();
    while (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS) {
        const QString op = advance().type;
        const QJsonObject right = parseMultiplicative();
        const double l = toNumber(node);
        const double r = toNumber(right);
        node = ast::number(op == TokenType::PLUS ? l + r : l - r);
    }
    return node;
}

QJsonObject Parser::parseMultiplicative() {
    QJsonObject node = parseUnary();
    while (peek().type == TokenType::STAR || peek().type == TokenType::SLASH) {
        const QString op = advance().type;
        const QJsonObject right = parseUnary();
        const double l = toNumber(node);
        const double r = toNumber(right);
        node = ast::number(op == TokenType::STAR ? l * r : l / r);
    }
    return node;
}

QJsonObject Parser::parseUnary() {
    if (peek().type == TokenType::PLUS) {
        advance();
        return parseUnary();
    }
    if (peek().type == TokenType::MINUS) {
        advance();
        const QJsonObject val = parseUnary();
        return ast::number(-toNumber(val));
    }
    return parsePrimary();
}

double Parser::toNumber(const QJsonObject& node) {
    if (node.value(QStringLiteral("type")).toString() != NodeKind::Number) {
        throw DslSyntaxError(QStringLiteral("Expected number"));
    }
    return node.value(QStringLiteral("value")).toDouble();
}

QJsonObject Parser::parsePrimary() {
    const Token token = peek();
    const QString tt = token.type;

    if (tt == TokenType::NUMBER) {
        advance();
        return ast::number(token.lexeme.toDouble());
    }
    if (tt == TokenType::STRING) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::String);
        node.insert(QStringLiteral("value"), token.lexeme);
        return node;
    }
    if (tt == TokenType::HEX) {
        advance();
        const QString hex = token.lexeme.mid(1);
        double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
        if (hex.length() == 3) {
            r = hexPairToInt(QString(2, hex.at(0)));
            g = hexPairToInt(QString(2, hex.at(1)));
            b = hexPairToInt(QString(2, hex.at(2)));
        } else if (hex.length() == 6) {
            r = hexPairToInt(hex.mid(0, 2));
            g = hexPairToInt(hex.mid(2, 2));
            b = hexPairToInt(hex.mid(4, 2));
        } else if (hex.length() == 8) {
            r = hexPairToInt(hex.mid(0, 2));
            g = hexPairToInt(hex.mid(2, 2));
            b = hexPairToInt(hex.mid(4, 2));
            a = hexPairToInt(hex.mid(6, 2)) / 255.0;
        }
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Color);
        QJsonArray value;
        value.append(r / 255.0);
        value.append(g / 255.0);
        value.append(b / 255.0);
        value.append(a);
        node.insert(QStringLiteral("value"), value);
        return node;
    }
    if (tt == TokenType::LBRACKET) {
        // Array literal -- comma-separated arg expressions, an alternate
        // input form for vec2/vec3/vec4 parameters.
        const int startLine = token.line;
        const int startCol = token.col;
        advance();
        QJsonArray elements;
        if (peek().type != TokenType::RBRACKET) {
            elements.append(parseArg());
            while (peek().type == TokenType::COMMA) {
                advance();
                elements.append(parseArg());
            }
        }
        if (peek().type != TokenType::RBRACKET) {
            const Token t = peek();
            throw DslSyntaxError::at(QStringLiteral("Expected ']'"), t.line, t.col);
        }
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::ArrayLiteral);
        node.insert(QStringLiteral("elements"), elements);
        node.insert(QStringLiteral("loc"), ast::loc(startLine, startCol));
        return node;
    }
    if (tt == TokenType::FUNC) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Func);
        node.insert(QStringLiteral("src"), token.lexeme);
        return node;
    }
    if (tt == TokenType::TRUE) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Boolean);
        node.insert(QStringLiteral("value"), true);
        return node;
    }
    if (tt == TokenType::FALSE) {
        advance();
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Boolean);
        node.insert(QStringLiteral("value"), false);
        return node;
    }
    if (tt == TokenType::IDENT) {
        const Token* next = tokenAt(current_ + 1);
        const Token* next2 = tokenAt(current_ + 2);
        if (token.lexeme == QStringLiteral("Math") && next && next->type == TokenType::DOT && next2
            && next2->type == TokenType::IDENT && next2->lexeme == QStringLiteral("PI")) {
            advance();
            advance();
            advance();
            return ast::number(3.141592653589793);
        }
        // member-then-call OR direct call -> parse as a chain (expression
        // context). See file header note: for 2+ dotted segments this
        // ultimately still fails inside parseCall (bug-for-bug parity).
        if ((next && next->type == TokenType::LPAREN) || hasCallAfterDot(current_)) {
            const QJsonArray chain = parseChain(QStringLiteral("expression"));
            if (chain.size() == 1) return chain.at(0).toObject();
            QJsonObject node;
            node.insert(QStringLiteral("type"), NodeKind::Chain);
            node.insert(QStringLiteral("chain"), chain);
            return node;
        }
        // dotted enum/member path (no call at the end).
        advance();
        QStringList path;
        path.append(token.lexeme);
        while (peek().type == TokenType::DOT) {
            const Token* n = tokenAt(current_ + 1);
            if (!n) break;
            const Token* after = tokenAt(current_ + 2);
            if (after && after->type == TokenType::LPAREN) break; // dot begins a call
            if (!memberTokenTypes().contains(n->type)) {
                throw DslSyntaxError::at(QStringLiteral("Expected identifier after '.'"), n->line, n->col);
            }
            advance(); // consume '.'
            advance(); // consume segment token
            path.append(n->lexeme);
        }
        if (path.size() > 1) {
            QJsonObject node;
            node.insert(QStringLiteral("type"), NodeKind::Member);
            QJsonArray pathArr;
            for (const QString& s : path) pathArr.append(s);
            node.insert(QStringLiteral("path"), pathArr);
            return node;
        }
        QJsonObject node;
        node.insert(QStringLiteral("type"), NodeKind::Ident);
        node.insert(QStringLiteral("name"), path.first());
        return node;
    }
    if (tt == TokenType::OUTPUT_REF) {
        advance();
        return refNode(NodeKind::OutputRef, token.lexeme);
    }
    if (tt == TokenType::SOURCE_REF) {
        advance();
        return refNode(NodeKind::SourceRef, token.lexeme);
    }
    if (tt == TokenType::VOL_REF) {
        advance();
        return refNode(NodeKind::VolRef, token.lexeme);
    }
    if (tt == TokenType::GEO_REF) {
        advance();
        return refNode(NodeKind::GeoRef, token.lexeme);
    }
    if (tt == TokenType::XYZ_REF) {
        advance();
        return refNode(NodeKind::XyzRef, token.lexeme);
    }
    if (tt == TokenType::VEL_REF) {
        advance();
        return refNode(NodeKind::VelRef, token.lexeme);
    }
    if (tt == TokenType::RGBA_REF) {
        advance();
        return refNode(NodeKind::RgbaRef, token.lexeme);
    }
    if (tt == TokenType::MESH_REF) {
        advance();
        return refNode(NodeKind::MeshRef, token.lexeme);
    }
    if (tt == TokenType::LPAREN) {
        advance();
        const QJsonObject expr = parseAdditive();
        expect(TokenType::RPAREN, QStringLiteral("Expect ')'"));
        return expr;
    }
    throw DslSyntaxError::at(QStringLiteral("Unexpected token %1").arg(token.type), token.line, token.col);
}

} // namespace

QJsonObject parse(const QJsonArray& tokens) {
    QVector<Token> tokenVec;
    tokenVec.reserve(tokens.size());
    for (const QJsonValue& v : tokens) {
        tokenVec.push_back(tokenFromJson(v.toObject()));
    }
    Parser parser(std::move(tokenVec));
    return parser.parseProgram();
}

} // namespace nm
