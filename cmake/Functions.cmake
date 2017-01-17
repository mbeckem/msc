# prepend_path(VARIABLE_NAME prefix files...)
# appends the path "prefix" to all files
# and stores the resulting list in VARIABLE_NAME.
#
# For example, prepend_path(result "a/b" file1 file2) returns
# the list "a/b/file1", "a/b/file2".
function(prepend_path var prefix)
   set(result "")
   foreach(f ${ARGN})
      list(APPEND result "${prefix}/${f}")
   endforeach()
   set(${var} "${result}" PARENT_SCOPE)
endfunction()
