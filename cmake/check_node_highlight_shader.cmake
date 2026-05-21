if(NOT DEFINED SHADER_FILE OR NOT EXISTS "${SHADER_FILE}")
    message(FATAL_ERROR "SHADER_FILE is required")
endif()

if(NOT DEFINED VERTEX_SHADER_FILE OR NOT EXISTS "${VERTEX_SHADER_FILE}")
    message(FATAL_ERROR "VERTEX_SHADER_FILE is required")
endif()

if(NOT DEFINED SELECTION_RENDERER_FILE OR NOT EXISTS "${SELECTION_RENDERER_FILE}")
    message(FATAL_ERROR "SELECTION_RENDERER_FILE is required")
endif()

file(READ "${SHADER_FILE}" shader)
file(READ "${VERTEX_SHADER_FILE}" vertex_shader)
file(READ "${SELECTION_RENDERER_FILE}" renderer)

if(NOT shader MATCHES "uniform[ \t]+bool[ \t]+uPointHighlight")
    message(FATAL_ERROR "scene.frag must expose uPointHighlight for node highlight points")
endif()

if(NOT shader MATCHES "gl_PointCoord")
    message(FATAL_ERROR "scene.frag must use gl_PointCoord to shape node highlight points")
endif()

if(NOT shader MATCHES "discard")
    message(FATAL_ERROR "scene.frag must discard fragments outside the circular node highlight")
endif()

if(NOT vertex_shader MATCHES "uniform[ \t]+float[ \t]+uPointSize")
    message(FATAL_ERROR "scene.vert must expose uPointSize for DPI-aware node highlight points")
endif()

if(NOT vertex_shader MATCHES "gl_PointSize[ \t]*=[ \t]*uPointSize")
    message(FATAL_ERROR "scene.vert must assign gl_PointSize from uPointSize")
endif()

if(NOT renderer MATCHES "uPointHighlight\"[^\n]*selHlMode_[ \t]*==[ \t]*1")
    message(FATAL_ERROR "SelectionRenderer must enable uPointHighlight only for node highlights")
endif()

if(NOT renderer MATCHES "uPointSize\"")
    message(FATAL_ERROR "SelectionRenderer must set uPointSize for node highlight points")
endif()

if(NOT renderer MATCHES "GL_PROGRAM_POINT_SIZE")
    message(FATAL_ERROR "SelectionRenderer must enable GL_PROGRAM_POINT_SIZE while drawing node points")
endif()
