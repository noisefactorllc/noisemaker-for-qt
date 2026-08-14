#include "frame_export.h"

#include <QOpenGLFunctions_4_1_Core>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nm {

namespace {

const char* const kVertexShader = R"GLSL(#version 330 core
void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)GLSL";

const char* const kFragmentShader = R"GLSL(#version 330 core
uniform sampler2D u_texture;
uniform int u_alphaMode;
out vec4 fragColor;
void main() {
    ivec2 sourceSize = textureSize(u_texture, 0);
    ivec2 sourceCoord = ivec2(
        int(gl_FragCoord.x),
        sourceSize.y - 1 - int(gl_FragCoord.y)
    );
    vec4 color = texelFetch(u_texture, sourceCoord, 0);
    if (u_alphaMode == 1) {
        color.a = 1.0;
    } else if (u_alphaMode == 2) {
        color.rgb *= color.a;
    }
    fragColor = color;
}
)GLSL";

int alphaModeValue(const QString& mode) {
    if (mode == QStringLiteral("straight")) return 0;
    if (mode == QStringLiteral("opaque")) return 1;
    if (mode == QStringLiteral("premultiplied")) return 2;
    throw std::invalid_argument(
        "Frame export alphaMode must be 'opaque', 'straight', or 'premultiplied'");
}

void validateDescriptor(const OutputDescriptor& descriptor) {
    if (descriptor.width <= 0) {
        throw std::invalid_argument("Frame export width must be a positive integer");
    }
    if (descriptor.height <= 0) {
        throw std::invalid_argument("Frame export height must be a positive integer");
    }
    if (descriptor.format != QStringLiteral("rgba8unorm")) {
        throw std::invalid_argument("Qt frame export format must be 'rgba8unorm'");
    }
    if (descriptor.colorSpace != QStringLiteral("srgb")
        && descriptor.colorSpace != QStringLiteral("display-p3")) {
        throw std::invalid_argument(
            "Qt frame export colorSpace must be 'srgb' or 'display-p3'");
    }
    alphaModeValue(descriptor.alphaMode);
    if (!std::isfinite(descriptor.fps) || descriptor.fps <= 0.0) {
        throw std::invalid_argument("Frame export fps must be finite and positive");
    }
    if (descriptor.width > std::numeric_limits<int>::max() / 4 / descriptor.height) {
        throw std::invalid_argument("Frame export dimensions are too large");
    }
}

} // namespace

struct FrameExportQueue::Record {
    int width = 0;
    int height = 0;
    int alphaMode = 0;
    unsigned int texture = 0;
    unsigned int framebuffer = 0;
    unsigned int pbo = 0;
    GLsync fence = nullptr;
    bool ready = false;
    bool created = false;
    bool pending = false;
    double timestamp = 0.0;
    std::function<void(const ExportFrame&, double)> onFrame;
    std::shared_ptr<ExportFrame> frame;
};

struct FrameExportQueue::Adapter {
    explicit Adapter(QOpenGLFunctions_4_1_Core* functions) : gl(functions) {
        if (!gl) throw std::invalid_argument("Qt frame export requires OpenGL functions");
    }

    QOpenGLFunctions_4_1_Core* gl = nullptr;
    unsigned int program = 0;
    unsigned int vao = 0;
    int textureLocation = -1;
    int alphaModeLocation = -1;

    unsigned int compileShader(unsigned int type, const char* source) {
        const unsigned int shader = gl->glCreateShader(type);
        if (!shader) throw std::runtime_error("Failed to create Qt frame export shader");
        gl->glShaderSource(shader, 1, &source, nullptr);
        gl->glCompileShader(shader);
        int ok = 0;
        gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            int length = 0;
            gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            QByteArray message(length, '\0');
            gl->glGetShaderInfoLog(shader, length, nullptr, message.data());
            gl->glDeleteShader(shader);
            throw std::runtime_error(
                ("Failed to compile Qt frame export shader: " + message).constData());
        }
        return shader;
    }

    void ensureProgram() {
        if (program) return;
        const unsigned int vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
        unsigned int fragment = 0;
        try {
            fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
            program = gl->glCreateProgram();
            if (!program) throw std::runtime_error("Failed to create Qt frame export program");
            gl->glAttachShader(program, vertex);
            gl->glAttachShader(program, fragment);
            gl->glLinkProgram(program);
            int ok = 0;
            gl->glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) {
                int length = 0;
                gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
                QByteArray message(length, '\0');
                gl->glGetProgramInfoLog(program, length, nullptr, message.data());
                throw std::runtime_error(
                    ("Failed to link Qt frame export program: " + message).constData());
            }
            textureLocation = gl->glGetUniformLocation(program, "u_texture");
            alphaModeLocation = gl->glGetUniformLocation(program, "u_alphaMode");
            gl->glGenVertexArrays(1, &vao);
            if (!vao) throw std::runtime_error("Failed to create Qt frame export vertex array");
        } catch (...) {
            if (program) gl->glDeleteProgram(program);
            program = 0;
            gl->glDeleteShader(vertex);
            if (fragment) gl->glDeleteShader(fragment);
            throw;
        }
        gl->glDeleteShader(vertex);
        gl->glDeleteShader(fragment);
    }

    void create(Record& slot, const OutputDescriptor& descriptor) {
        validateDescriptor(descriptor);
        ensureProgram();
        slot.width = descriptor.width;
        slot.height = descriptor.height;
        slot.alphaMode = alphaModeValue(descriptor.alphaMode);
        slot.frame = std::make_shared<ExportFrame>();
        slot.frame->width = slot.width;
        slot.frame->height = slot.height;
        slot.frame->rowStride = slot.width * 4;
        slot.frame->data.resize(slot.frame->rowStride * slot.frame->height);
        try {
            gl->glGenTextures(1, &slot.texture);
            if (!slot.texture) throw std::runtime_error("Failed to create Qt frame export texture");
            gl->glBindTexture(GL_TEXTURE_2D, slot.texture);
            gl->glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, slot.width, slot.height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            gl->glGenFramebuffers(1, &slot.framebuffer);
            if (!slot.framebuffer) {
                throw std::runtime_error("Failed to create Qt frame export framebuffer");
            }
            gl->glBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
            gl->glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.texture, 0);
            if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("Qt frame export framebuffer is incomplete");
            }

            gl->glGenBuffers(1, &slot.pbo);
            if (!slot.pbo) throw std::runtime_error("Failed to create Qt frame export pixel buffer");
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
            gl->glBufferData(
                GL_PIXEL_PACK_BUFFER,
                static_cast<qptrdiff>(slot.width) * slot.height * 4,
                nullptr,
                GL_STREAM_READ);
            slot.created = true;
        } catch (...) {
            destroy(slot);
            resetState();
            throw;
        }
        resetState();
    }

    void begin(Record& slot, const GpuSurface& source) {
        if (!slot.created || slot.pending || !source.texture) {
            throw std::runtime_error("Qt frame export slot or source is not usable");
        }
        if (source.width != slot.width || source.height != slot.height) {
            throw std::runtime_error(
                QStringLiteral("Qt frame export source extent %1x%2 does not match configured extent %3x%4")
                    .arg(source.width).arg(source.height).arg(slot.width).arg(slot.height)
                    .toStdString());
        }

        try {
            gl->glBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
            gl->glViewport(0, 0, slot.width, slot.height);
            gl->glDisable(GL_BLEND);
            gl->glDisable(GL_DEPTH_TEST);
            gl->glDisable(GL_SCISSOR_TEST);
            gl->glDisable(GL_CULL_FACE);
            gl->glUseProgram(program);
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, source.texture);
            gl->glUniform1i(textureLocation, 0);
            gl->glUniform1i(alphaModeLocation, slot.alphaMode);
            gl->glBindVertexArray(vao);
            gl->glDrawArrays(GL_TRIANGLES, 0, 3);

            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
            gl->glReadPixels(
                0, 0, slot.width, slot.height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            slot.fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (!slot.fence) throw std::runtime_error("Failed to create Qt frame export fence");
            gl->glFlush();
            slot.pending = true;
            slot.ready = false;
        } catch (...) {
            deleteFence(slot);
            resetState();
            throw;
        }
        resetState();
    }

    bool poll(Record& slot) {
        if (!slot.created || !slot.pending || !slot.fence) {
            throw std::runtime_error("Qt frame export slot has no pending fence");
        }
        if (slot.ready) return true;
        const unsigned int status = gl->glClientWaitSync(slot.fence, 0, 0);
        if (status == GL_TIMEOUT_EXPIRED) return false;
        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
            slot.ready = true;
            return true;
        }
        deleteFence(slot);
        if (status == GL_WAIT_FAILED) {
            throw std::runtime_error("Qt frame export fence wait failed");
        }
        throw std::runtime_error("Unexpected Qt frame export fence status");
    }

    std::shared_ptr<const ExportFrame> read(Record& slot) {
        if (!slot.ready || !slot.fence || !slot.frame) {
            throw std::runtime_error("Qt frame export slot is not ready");
        }
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        gl->glGetBufferSubData(
            GL_PIXEL_PACK_BUFFER, 0,
            slot.frame->data.size(), slot.frame->data.data());
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        deleteFence(slot);
        return slot.frame;
    }

    void deleteFence(Record& slot) {
        if (slot.fence) gl->glDeleteSync(slot.fence);
        slot.fence = nullptr;
        slot.ready = false;
        slot.pending = false;
    }

    void destroy(Record& slot) {
        deleteFence(slot);
        if (slot.pbo) gl->glDeleteBuffers(1, &slot.pbo);
        if (slot.framebuffer) gl->glDeleteFramebuffers(1, &slot.framebuffer);
        if (slot.texture) gl->glDeleteTextures(1, &slot.texture);
        slot.pbo = 0;
        slot.framebuffer = 0;
        slot.texture = 0;
        slot.created = false;
        slot.frame.reset();
    }

    void destroyProgram() {
        if (vao) gl->glDeleteVertexArrays(1, &vao);
        if (program) gl->glDeleteProgram(program);
        vao = 0;
        program = 0;
    }

    void resetState() {
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        gl->glBindVertexArray(0);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glUseProgram(0);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

FrameExportQueue::FrameExportQueue(
    QOpenGLFunctions_4_1_Core* gl,
    FrameExportOptions options)
    : m_adapter(std::make_unique<Adapter>(gl)),
      m_onFrame(std::move(options.onFrame)),
      m_onError(std::move(options.onError)) {
    if (options.slotCount < 2 || options.slotCount > 8) {
        throw std::invalid_argument("Frame export slots must be an integer from 2 through 8");
    }
    m_slots.reserve(static_cast<std::size_t>(options.slotCount));
    for (int index = 0; index < options.slotCount; ++index) {
        m_slots.push_back(std::make_unique<Record>());
    }
}

FrameExportQueue::~FrameExportQueue() {
    if (!m_closed) {
        try {
            close();
        } catch (...) {
        }
    }
}

void FrameExportQueue::configure(const OutputDescriptor& descriptor) {
    if (m_closed) return;
    destroySlots();
    m_configured = false;
    try {
        for (const auto& record : m_slots) m_adapter->create(*record, descriptor);
    } catch (...) {
        try {
            destroySlots();
        } catch (...) {
            report(std::current_exception());
        }
        throw;
    }
    m_configured = true;
}

bool FrameExportQueue::submit(const GpuSurface& surface, double timestamp) {
    if (!m_onFrame) {
        throw std::runtime_error("Frame export sink requires an onFrame callback");
    }
    return enqueue(surface, timestamp, m_onFrame);
}

bool FrameExportQueue::enqueue(
    const GpuSurface& surface,
    double timestamp,
    std::function<void(const ExportFrame&, double)> onFrame) {
    if (!onFrame) throw std::invalid_argument("Frame export callback must be callable");
    if (!m_configured || m_closed) {
        ++m_stats.dropped;
        return false;
    }
    Record* availableRecord = nullptr;
    for (const auto& record : m_slots) {
        if (!record->pending) {
            availableRecord = record.get();
            break;
        }
    }
    if (!availableRecord) {
        ++m_stats.dropped;
        return false;
    }

    availableRecord->timestamp = timestamp;
    availableRecord->onFrame = std::move(onFrame);
    try {
        m_adapter->begin(*availableRecord, surface);
    } catch (...) {
        release(*availableRecord);
        ++m_stats.failed;
        report(std::current_exception());
        return false;
    }
    ++m_stats.accepted;
    return true;
}

void FrameExportQueue::poll() {
    if (!m_configured || m_closed) return;
    for (const auto& recordPtr : m_slots) {
        Record& record = *recordPtr;
        if (!record.pending) continue;
        std::shared_ptr<const ExportFrame> frame;
        try {
            if (!m_adapter->poll(record)) continue;
            frame = m_adapter->read(record);
        } catch (...) {
            release(record);
            ++m_stats.failed;
            report(std::current_exception());
            continue;
        }
        const double timestamp = record.timestamp;
        auto onFrame = std::move(record.onFrame);
        release(record);
        try {
            onFrame(*frame, timestamp);
            ++m_stats.completed;
        } catch (...) {
            ++m_stats.failed;
            report(std::current_exception());
        }
    }
}

bool FrameExportQueue::available() const {
    if (!m_configured || m_closed) return false;
    for (const auto& record : m_slots) {
        if (!record->pending) return true;
    }
    return false;
}

FrameExportStats FrameExportQueue::stats() const {
    return m_stats;
}

void FrameExportQueue::release(Record& record) {
    record.pending = false;
    record.ready = false;
    record.timestamp = 0.0;
    record.onFrame = {};
}

void FrameExportQueue::destroySlots() {
    for (const auto& record : m_slots) {
        if (record->created) m_adapter->destroy(*record);
        release(*record);
    }
}

void FrameExportQueue::close(bool backendLost) {
    if (m_closed) return;
    m_closed = true;
    m_configured = false;
    if (backendLost) {
        for (const auto& record : m_slots) {
            record->texture = 0;
            record->framebuffer = 0;
            record->pbo = 0;
            record->fence = nullptr;
            record->created = false;
            record->frame.reset();
            release(*record);
        }
    } else {
        destroySlots();
        m_adapter->destroyProgram();
    }
}

void FrameExportQueue::report(std::exception_ptr error) const {
    if (!m_onError) return;
    try {
        m_onError(std::move(error));
    } catch (...) {
    }
}

} // namespace nm
