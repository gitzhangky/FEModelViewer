if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is not defined")
endif()

file(READ "${SOURCE_FILE}" source_text)

if(source_text MATCHES "QOpenGLFramebufferObject::bindDefault[ \t]*\\(")
    message(FATAL_ERROR "QOpenGLWidget rendering must restore defaultFramebufferObject(), not bindDefault()/FBO 0")
endif()

if(NOT source_text MATCHES "defaultFramebufferObject[ \t]*\\(")
    message(FATAL_ERROR "GLWidget should restore QOpenGLWidget's defaultFramebufferObject() after custom FBO picking")
endif()

# 拾取 FBO 必须用原始 glBindFramebuffer 绑定，不能用 pickFbo_->bind()。
# 后者会更新 Qt 内部 current_fbo 追踪，而恢复时只能用原始 GL（widget 的 default FBO
# 不是 0），追踪与实际状态错位会导致 Windows GL2 文字引擎在拾取后 drawText 静默失败。
if(source_text MATCHES "pickFbo_->bind[ \t]*\\(")
    message(FATAL_ERROR "pickFbo_ must be bound via glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_->handle()), not pickFbo_->bind(), to avoid corrupting Qt's current_fbo tracking and breaking QPainter text on Windows")
endif()
