# see arithmetic/CMakeLists.txt for comments

list (APPEND IBEX_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_ExtendedSystem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_ExtendedSystem.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_FncActiveCtrs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_FncActiveCtrs.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_FncKuhnTucker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_FncKuhnTucker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_KuhnTuckerSystem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_KuhnTuckerSystem.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_NormalizedSystem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_NormalizedSystem.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_System.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_System.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_SystemFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_SystemFactory.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ibex_SystemMerge.cpp
  )

list (APPEND IBEX_INCDIRS ${CMAKE_CURRENT_SOURCE_DIR})

# Propagate new values of list to parent scope
set (IBEX_SRC ${IBEX_SRC} PARENT_SCOPE)
set (IBEX_INCDIRS ${IBEX_INCDIRS} PARENT_SCOPE)
