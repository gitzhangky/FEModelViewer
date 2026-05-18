// GL 状态 RAII 守卫，防止状态泄漏导致 Windows Qt5 GL2 文字引擎静默失败
#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>

#include <functional>
#include <utility>

class ScopedBufferBind {
public:
    explicit ScopedBufferBind(QOpenGLBuffer& buf) : buf_(buf) { buf_.bind(); }
    ~ScopedBufferBind() { buf_.release(); }
    ScopedBufferBind(const ScopedBufferBind&) = delete;
    ScopedBufferBind& operator=(const ScopedBufferBind&) = delete;

private:
    QOpenGLBuffer& buf_;
};

class ScopedRawBufferBind {
public:
    ScopedRawBufferBind(QOpenGLFunctions* gl, GLenum target, GLuint buffer)
        : gl_(gl), target_(target) {
        gl_->glGetIntegerv(bindingEnum(target), &prev_);
        gl_->glBindBuffer(target_, buffer);
    }
    ~ScopedRawBufferBind() {
        gl_->glBindBuffer(target_, static_cast<GLuint>(prev_));
    }
    ScopedRawBufferBind(const ScopedRawBufferBind&) = delete;
    ScopedRawBufferBind& operator=(const ScopedRawBufferBind&) = delete;

private:
    static GLenum bindingEnum(GLenum target) {
        switch (target) {
        case GL_ARRAY_BUFFER: return GL_ARRAY_BUFFER_BINDING;
        case GL_ELEMENT_ARRAY_BUFFER: return GL_ELEMENT_ARRAY_BUFFER_BINDING;
        default: return GL_ARRAY_BUFFER_BINDING;
        }
    }

    QOpenGLFunctions* gl_;
    GLenum target_;
    GLint prev_ = 0;
};

class ScopedFramebufferBind {
public:
    ScopedFramebufferBind(QOpenGLFunctions* gl,
                          GLuint framebuffer,
                          std::function<void()> restore = {})
        : gl_(gl), restore_(std::move(restore)) {
        gl_->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_);
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    }
    ~ScopedFramebufferBind() {
        if (restore_) {
            restore_();
        } else {
            gl_->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_));
        }
    }
    ScopedFramebufferBind(const ScopedFramebufferBind&) = delete;
    ScopedFramebufferBind& operator=(const ScopedFramebufferBind&) = delete;

private:
    QOpenGLFunctions* gl_;
    GLint prev_ = 0;
    std::function<void()> restore_;
};

class ScopedViewport {
public:
    explicit ScopedViewport(QOpenGLFunctions* gl) : gl_(gl) {
        gl_->glGetIntegerv(GL_VIEWPORT, prev_);
    }
    ~ScopedViewport() {
        gl_->glViewport(prev_[0], prev_[1], prev_[2], prev_[3]);
    }
    ScopedViewport(const ScopedViewport&) = delete;
    ScopedViewport& operator=(const ScopedViewport&) = delete;

private:
    QOpenGLFunctions* gl_;
    GLint prev_[4] = {0, 0, 0, 0};
};

class ScopedDepthTest {
public:
    ScopedDepthTest(QOpenGLFunctions* gl, bool enable) : gl_(gl) {
        gl_->glGetBooleanv(GL_DEPTH_TEST, &prev_);
        if (enable) gl_->glEnable(GL_DEPTH_TEST);
        else        gl_->glDisable(GL_DEPTH_TEST);
    }
    ~ScopedDepthTest() {
        if (prev_) gl_->glEnable(GL_DEPTH_TEST);
        else       gl_->glDisable(GL_DEPTH_TEST);
    }
    ScopedDepthTest(const ScopedDepthTest&) = delete;
    ScopedDepthTest& operator=(const ScopedDepthTest&) = delete;

private:
    QOpenGLFunctions* gl_;
    GLboolean prev_ = GL_FALSE;
};

class ScopedBlend {
public:
    ScopedBlend(QOpenGLFunctions* gl, bool enable) : gl_(gl) {
        gl_->glGetBooleanv(GL_BLEND, &prev_);
        if (enable) gl_->glEnable(GL_BLEND);
        else        gl_->glDisable(GL_BLEND);
    }
    ~ScopedBlend() {
        if (prev_) gl_->glEnable(GL_BLEND);
        else       gl_->glDisable(GL_BLEND);
    }
    ScopedBlend(const ScopedBlend&) = delete;
    ScopedBlend& operator=(const ScopedBlend&) = delete;

private:
    QOpenGLFunctions* gl_;
    GLboolean prev_ = GL_FALSE;
};

class ScopedClearColor {
public:
    explicit ScopedClearColor(QOpenGLFunctions* gl) : gl_(gl) {
        gl_->glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_);
    }
    ~ScopedClearColor() {
        gl_->glClearColor(prev_[0], prev_[1], prev_[2], prev_[3]);
    }
    ScopedClearColor(const ScopedClearColor&) = delete;
    ScopedClearColor& operator=(const ScopedClearColor&) = delete;

private:
    QOpenGLFunctions* gl_;
    GLfloat prev_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};
