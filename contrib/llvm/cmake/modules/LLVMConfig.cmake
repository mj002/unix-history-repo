function(get_system_libs return_var)
  # Returns in `return_var' a list of system libraries used by LLVM.
  if( NOT MSVC )
    if( MINGW )
      set(system_libs ${system_libs} imagehlp psapi)
    elseif( CMAKE_HOST_UNIX )
      if( HAVE_LIBDL )
	set(system_libs ${system_libs} ${CMAKE_DL_LIBS})
      endif()
      if( LLVM_ENABLE_THREADS AND HAVE_LIBPTHREAD )
	set(system_libs ${system_libs} pthread)
      endif()
    endif( MINGW )
  endif( NOT MSVC )
  set(${return_var} ${system_libs} PARENT_SCOPE)
endfunction(get_system_libs)


macro(llvm_config executable)
  explicit_llvm_config(${executable} ${ARGN})
endmacro(llvm_config)


function(explicit_llvm_config executable)
  set( link_components ${ARGN} )

  explicit_map_components_to_libraries(LIBRARIES ${link_components})
  target_link_libraries(${executable} ${LIBRARIES})
endfunction(explicit_llvm_config)


function(explicit_map_components_to_libraries out_libs)
  set( link_components ${ARGN} )
  foreach(c ${link_components})
    # add codegen, asmprinter, asmparser, disassembler
    list(FIND LLVM_TARGETS_TO_BUILD ${c} idx)
    if( NOT idx LESS 0 )
      list(FIND llvm_libs "LLVM${c}CodeGen" idx)
      if( NOT idx LESS 0 )
	list(APPEND expanded_components "LLVM${c}CodeGen")
      else()
	list(FIND llvm_libs "LLVM${c}" idx)
	if( NOT idx LESS 0 )
	  list(APPEND expanded_components "LLVM${c}")
	else()
	  message(FATAL_ERROR "Target ${c} is not in the set of libraries.")
	endif()
      endif()
      list(FIND llvm_libs "LLVM${c}AsmPrinter" asmidx)
      if( NOT asmidx LESS 0 )
        list(APPEND expanded_components "LLVM${c}AsmPrinter")
      endif()
      list(FIND llvm_libs "LLVM${c}AsmParser" asmidx)
      if( NOT asmidx LESS 0 )
        list(APPEND expanded_components "LLVM${c}AsmParser")
      endif()
      list(FIND llvm_libs "LLVM${c}Info" asmidx)
      if( NOT asmidx LESS 0 )
        list(APPEND expanded_components "LLVM${c}Info")
      endif()
      list(FIND llvm_libs "LLVM${c}Disassembler" asmidx)
      if( NOT asmidx LESS 0 )
        list(APPEND expanded_components "LLVM${c}Disassembler")
      endif()
    elseif( c STREQUAL "native" )
      list(APPEND expanded_components "LLVM${LLVM_NATIVE_ARCH}CodeGen")
    elseif( c STREQUAL "nativecodegen" )
      list(APPEND expanded_components "LLVM${LLVM_NATIVE_ARCH}CodeGen")
    elseif( c STREQUAL "backend" )
      # same case as in `native'.
    elseif( c STREQUAL "engine" )
      # TODO: as we assume we are on X86, this is `jit'.
      list(APPEND expanded_components "LLVMJIT")
    elseif( c STREQUAL "all" )
      list(APPEND expanded_components ${llvm_libs})
    else( NOT idx LESS 0 )
      list(APPEND expanded_components LLVM${c})
    endif( NOT idx LESS 0 )
  endforeach(c)
  # We must match capitalization.
  string(TOUPPER "${llvm_libs}" capitalized_libs)
  list(REMOVE_DUPLICATES expanded_components)
  list(LENGTH expanded_components lst_size)
  set(result "")
  while( 0 LESS ${lst_size} )
    list(GET expanded_components 0 c)
    string(TOUPPER "${c}" capitalized)
    list(FIND capitalized_libs ${capitalized} idx)
    if( idx LESS 0 )
      message(FATAL_ERROR "Library ${c} not found in list of llvm libraries.")
    endif( idx LESS 0 )
    list(GET llvm_libs ${idx} canonical_lib)
    list(REMOVE_ITEM result ${canonical_lib})
    list(APPEND result ${canonical_lib})
    foreach(c ${MSVC_LIB_DEPS_${canonical_lib}})
      list(REMOVE_ITEM expanded_components ${c})
    endforeach()
    list(APPEND expanded_components ${MSVC_LIB_DEPS_${canonical_lib}})
    list(REMOVE_AT expanded_components 0)
    list(LENGTH expanded_components lst_size)
  endwhile( 0 LESS ${lst_size} )
  set(${out_libs} ${result} PARENT_SCOPE)
endfunction(explicit_map_components_to_libraries)


# The library dependency data is contained in the file
# LLVMLibDeps.cmake on this directory. It is automatically generated
# by tools/llvm-config/CMakeLists.txt when the build comprises all the
# targets and we are on a environment Posix enough to build the
# llvm-config script. This, in practice, just excludes MSVC.

# When you remove or rename a library from the build, be sure to
# remove its file from lib/ as well, or the GenLibDeps.pl script will
# include it on its analysis!

# The format generated by GenLibDeps.pl

# LLVMARMAsmPrinter.o: LLVMARMCodeGen.o libLLVMAsmPrinter.a libLLVMCodeGen.a libLLVMCore.a libLLVMSupport.a libLLVMTarget.a

# is translated to:

# set(MSVC_LIB_DEPS_LLVMARMAsmPrinter LLVMARMCodeGen LLVMAsmPrinter LLVMCodeGen LLVMCore LLVMSupport LLVMTarget)

# It is necessary to remove the `lib' prefix and the `.a'.

# This 'sed' script should do the trick:
# sed -e s'#\.a##g' -e 's#libLLVM#LLVM#g' -e 's#: # #' -e 's#\(.*\)#set(MSVC_LIB_DEPS_\1)#' ~/llvm/tools/llvm-config/LibDeps.txt

include(LLVMLibDeps)
