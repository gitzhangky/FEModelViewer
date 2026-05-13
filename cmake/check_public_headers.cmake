if(NOT DEFINED INSTALLED_HEADERS)
    message(FATAL_ERROR "INSTALLED_HEADERS is not defined")
endif()

set(required_headers
    Theme.h
)

foreach(header IN LISTS required_headers)
    list(FIND INSTALLED_HEADERS "${header}" header_index)
    if(header_index EQUAL -1)
        message(FATAL_ERROR "${header} must be installed because it is used by public FERender headers")
    endif()
endforeach()
