if(NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "SOURCE_FILE is not defined")
endif()

file(READ "${SOURCE_FILE}" source_text)

if(source_text MATCHES "#include[ \t]+\"FEModelPanel\\.h\"")
    message(FATAL_ERROR "CLI parse mode must not include FEModelPanel.h")
endif()

if(source_text MATCHES "FEModelPanel[ \t]+[A-Za-z_]")
    message(FATAL_ERROR "CLI parse mode must use FEParser directly instead of FEModelPanel")
endif()
