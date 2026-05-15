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
