/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.36
 * 
 * This file is not intended to be easily readable and contains a number of 
 * coding conventions designed to improve portability and efficiency. Do not make
 * changes to this file unless you know what you are doing--modify the SWIG 
 * interface file instead. 
 * ----------------------------------------------------------------------------- */


#ifdef __cplusplus
template<typename T> class SwigValueWrapper {
    T *tt;
public:
    SwigValueWrapper() : tt(0) { }
    SwigValueWrapper(const SwigValueWrapper<T>& rhs) : tt(new T(*rhs.tt)) { }
    SwigValueWrapper(const T& t) : tt(new T(t)) { }
    ~SwigValueWrapper() { delete tt; } 
    SwigValueWrapper& operator=(const T& t) { delete tt; tt = new T(t); return *this; }
    operator T&() const { return *tt; }
    T *operator&() { return tt; }
private:
    SwigValueWrapper& operator=(const SwigValueWrapper<T>& rhs);
};

template <typename T> T SwigValueInit() {
  return T();
}
#endif

/* -----------------------------------------------------------------------------
 *  This section contains generic SWIG labels for method/variable
 *  declarations/attributes, and other compiler dependent labels.
 * ----------------------------------------------------------------------------- */

/* template workaround for compilers that cannot correctly implement the C++ standard */
#ifndef SWIGTEMPLATEDISAMBIGUATOR
# if defined(__SUNPRO_CC) && (__SUNPRO_CC <= 0x560)
#  define SWIGTEMPLATEDISAMBIGUATOR template
# elif defined(__HP_aCC)
/* Needed even with `aCC -AA' when `aCC -V' reports HP ANSI C++ B3910B A.03.55 */
/* If we find a maximum version that requires this, the test would be __HP_aCC <= 35500 for A.03.55 */
#  define SWIGTEMPLATEDISAMBIGUATOR template
# else
#  define SWIGTEMPLATEDISAMBIGUATOR
# endif
#endif

/* inline attribute */
#ifndef SWIGINLINE
# if defined(__cplusplus) || (defined(__GNUC__) && !defined(__STRICT_ANSI__))
#   define SWIGINLINE inline
# else
#   define SWIGINLINE
# endif
#endif

/* attribute recognised by some compilers to avoid 'unused' warnings */
#ifndef SWIGUNUSED
# if defined(__GNUC__)
#   if !(defined(__cplusplus)) || (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
#     define SWIGUNUSED __attribute__ ((__unused__)) 
#   else
#     define SWIGUNUSED
#   endif
# elif defined(__ICC)
#   define SWIGUNUSED __attribute__ ((__unused__)) 
# else
#   define SWIGUNUSED 
# endif
#endif

#ifndef SWIGUNUSEDPARM
# ifdef __cplusplus
#   define SWIGUNUSEDPARM(p)
# else
#   define SWIGUNUSEDPARM(p) p SWIGUNUSED 
# endif
#endif

/* internal SWIG method */
#ifndef SWIGINTERN
# define SWIGINTERN static SWIGUNUSED
#endif

/* internal inline SWIG method */
#ifndef SWIGINTERNINLINE
# define SWIGINTERNINLINE SWIGINTERN SWIGINLINE
#endif

/* exporting methods */
#if (__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#  ifndef GCC_HASCLASSVISIBILITY
#    define GCC_HASCLASSVISIBILITY
#  endif
#endif

#ifndef SWIGEXPORT
# if defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
#   if defined(STATIC_LINKED)
#     define SWIGEXPORT
#   else
#     define SWIGEXPORT __declspec(dllexport)
#   endif
# else
#   if defined(__GNUC__) && defined(GCC_HASCLASSVISIBILITY)
#     define SWIGEXPORT __attribute__ ((visibility("default")))
#   else
#     define SWIGEXPORT
#   endif
# endif
#endif

/* calling conventions for Windows */
#ifndef SWIGSTDCALL
# if defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
#   define SWIGSTDCALL __stdcall
# else
#   define SWIGSTDCALL
# endif 
#endif

/* Deal with Microsoft's attempt at deprecating C standard runtime functions */
#if !defined(SWIG_NO_CRT_SECURE_NO_DEPRECATE) && defined(_MSC_VER) && !defined(_CRT_SECURE_NO_DEPRECATE)
# define _CRT_SECURE_NO_DEPRECATE
#endif

/* Deal with Microsoft's attempt at deprecating methods in the standard C++ library */
#if !defined(SWIG_NO_SCL_SECURE_NO_DEPRECATE) && defined(_MSC_VER) && !defined(_SCL_SECURE_NO_DEPRECATE)
# define _SCL_SECURE_NO_DEPRECATE
#endif



#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32) || defined(__CYGWIN32__)
#  define DllExport   __declspec( dllexport )
#  define SWIGSTDCALL __stdcall
#else
#  define DllExport  
#  define SWIGSTDCALL
#endif 


#ifdef __cplusplus
#  include <new>
#endif




/* Support for throwing Ada exceptions from C/C++ */

typedef enum 
{
  SWIG_AdaException,
  SWIG_AdaOutOfMemoryException,
  SWIG_AdaIndexOutOfRangeException,
  SWIG_AdaDivideByZeroException,
  SWIG_AdaArgumentOutOfRangeException,
  SWIG_AdaNullReferenceException
} SWIG_AdaExceptionCodes;


typedef void (SWIGSTDCALL* SWIG_AdaExceptionCallback_t)(const char *);


typedef struct 
{
  SWIG_AdaExceptionCodes code;
  SWIG_AdaExceptionCallback_t callback;
} 
  SWIG_AdaExceptions_t;


static 
SWIG_AdaExceptions_t 
SWIG_ada_exceptions[] = 
{
  { SWIG_AdaException, NULL },
  { SWIG_AdaOutOfMemoryException, NULL },
  { SWIG_AdaIndexOutOfRangeException, NULL },
  { SWIG_AdaDivideByZeroException, NULL },
  { SWIG_AdaArgumentOutOfRangeException, NULL },
  { SWIG_AdaNullReferenceException, NULL } 
};


static 
void 
SWIG_AdaThrowException (SWIG_AdaExceptionCodes code, const char *msg) 
{
  SWIG_AdaExceptionCallback_t callback = SWIG_ada_exceptions[SWIG_AdaException].callback;
  if (code >=0 && (size_t)code < sizeof(SWIG_ada_exceptions)/sizeof(SWIG_AdaExceptions_t)) {
    callback = SWIG_ada_exceptions[code].callback;
  }
  callback(msg);
}



#ifdef __cplusplus
extern "C" 
#endif

DllExport void SWIGSTDCALL SWIGRegisterExceptionCallbacks_LLVM_link_time_Optimizer (SWIG_AdaExceptionCallback_t systemException,
                                                                   SWIG_AdaExceptionCallback_t outOfMemory, 
                                                                   SWIG_AdaExceptionCallback_t indexOutOfRange, 
                                                                   SWIG_AdaExceptionCallback_t divideByZero, 
                                                                   SWIG_AdaExceptionCallback_t argumentOutOfRange,
                                                                   SWIG_AdaExceptionCallback_t nullReference) 
{
  SWIG_ada_exceptions [SWIG_AdaException].callback                   = systemException;
  SWIG_ada_exceptions [SWIG_AdaOutOfMemoryException].callback        = outOfMemory;
  SWIG_ada_exceptions [SWIG_AdaIndexOutOfRangeException].callback    = indexOutOfRange;
  SWIG_ada_exceptions [SWIG_AdaDivideByZeroException].callback       = divideByZero;
  SWIG_ada_exceptions [SWIG_AdaArgumentOutOfRangeException].callback = argumentOutOfRange;
  SWIG_ada_exceptions [SWIG_AdaNullReferenceException].callback      = nullReference;
}


/* Callback for returning strings to Ada without leaking memory */

typedef char * (SWIGSTDCALL* SWIG_AdaStringHelperCallback)(const char *);
static SWIG_AdaStringHelperCallback SWIG_ada_string_callback = NULL;



/* probably obsolete ...
#ifdef __cplusplus
extern "C" 
#endif
DllExport void SWIGSTDCALL SWIGRegisterStringCallback_LLVM_link_time_Optimizer(SWIG_AdaStringHelperCallback callback) {
  SWIG_ada_string_callback = callback;
}
*/



/* Contract support */

#define SWIG_contract_assert(nullreturn, expr, msg) if (!(expr)) {SWIG_AdaThrowException(SWIG_AdaArgumentOutOfRangeException, msg); return nullreturn; } else


#define protected public
#define private   public

#include "llvm-c/lto.h"
#include "llvm-c/LinkTimeOptimizer.h"



//  struct LLVMCtxt;


#undef protected
#undef private
#ifdef __cplusplus 
extern "C" {
#endif
DllExport char * SWIGSTDCALL Ada_lto_get_version (
  )
{
  char * jresult ;
  char *result = 0 ;
  
  result = (char *)lto_get_version();
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport char * SWIGSTDCALL Ada_lto_get_error_message (
  )
{
  char * jresult ;
  char *result = 0 ;
  
  result = (char *)lto_get_error_message();
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_module_is_object_file (
  char * jarg1
  )
{
  unsigned int jresult ;
  char *arg1 = (char *) 0 ;
  bool result;
  
  arg1 = jarg1; 
  
  result = (bool)lto_module_is_object_file((char const *)arg1);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_module_is_object_file_for_target (
  char * jarg1
  ,
  
  char * jarg2
  )
{
  unsigned int jresult ;
  char *arg1 = (char *) 0 ;
  char *arg2 = (char *) 0 ;
  bool result;
  
  arg1 = jarg1; 
  
  arg2 = jarg2; 
  
  result = (bool)lto_module_is_object_file_for_target((char const *)arg1,(char const *)arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_module_is_object_file_in_memory (
  void* jarg1
  ,
  
  size_t jarg2
  )
{
  unsigned int jresult ;
  void *arg1 = (void *) 0 ;
  size_t arg2 ;
  bool result;
  
  arg1 = (void *)jarg1; 
  
  
  arg2 = (size_t) jarg2; 
  
  
  result = (bool)lto_module_is_object_file_in_memory((void const *)arg1,arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_module_is_object_file_in_memory_for_target (
  void* jarg1
  ,
  
  size_t jarg2
  ,
  
  char * jarg3
  )
{
  unsigned int jresult ;
  void *arg1 = (void *) 0 ;
  size_t arg2 ;
  char *arg3 = (char *) 0 ;
  bool result;
  
  arg1 = (void *)jarg1; 
  
  
  arg2 = (size_t) jarg2; 
  
  
  arg3 = jarg3; 
  
  result = (bool)lto_module_is_object_file_in_memory_for_target((void const *)arg1,arg2,(char const *)arg3);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport void * SWIGSTDCALL Ada_lto_module_create (
  char * jarg1
  )
{
  void * jresult ;
  char *arg1 = (char *) 0 ;
  lto_module_t result;
  
  arg1 = jarg1; 
  
  result = (lto_module_t)lto_module_create((char const *)arg1);
  jresult = (void *) result;      
  
  
  
  return jresult;
  
}



DllExport void * SWIGSTDCALL Ada_lto_module_create_from_memory (
  void* jarg1
  ,
  
  size_t jarg2
  )
{
  void * jresult ;
  void *arg1 = (void *) 0 ;
  size_t arg2 ;
  lto_module_t result;
  
  arg1 = (void *)jarg1; 
  
  
  arg2 = (size_t) jarg2; 
  
  
  result = (lto_module_t)lto_module_create_from_memory((void const *)arg1,arg2);
  jresult = (void *) result;      
  
  
  
  return jresult;
  
}



DllExport void SWIGSTDCALL Ada_lto_module_dispose (
  void * jarg1
  )
{
  lto_module_t arg1 = (lto_module_t) 0 ;
  
  arg1 = (lto_module_t)jarg1; 
  
  lto_module_dispose(arg1);
  
  
}



DllExport char * SWIGSTDCALL Ada_lto_module_get_target_triple (
  void * jarg1
  )
{
  char * jresult ;
  lto_module_t arg1 = (lto_module_t) 0 ;
  char *result = 0 ;
  
  arg1 = (lto_module_t)jarg1; 
  
  result = (char *)lto_module_get_target_triple(arg1);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_module_get_num_symbols (
  void * jarg1
  )
{
  unsigned int jresult ;
  lto_module_t arg1 = (lto_module_t) 0 ;
  unsigned int result;
  
  arg1 = (lto_module_t)jarg1; 
  
  result = (unsigned int)lto_module_get_num_symbols(arg1);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport char * SWIGSTDCALL Ada_lto_module_get_symbol_name (
  void * jarg1
  ,
  
  unsigned int jarg2
  )
{
  char * jresult ;
  lto_module_t arg1 = (lto_module_t) 0 ;
  unsigned int arg2 ;
  char *result = 0 ;
  
  arg1 = (lto_module_t)jarg1; 
  
  
  arg2 = (unsigned int) jarg2; 
  
  
  result = (char *)lto_module_get_symbol_name(arg1,arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport int SWIGSTDCALL Ada_lto_module_get_symbol_attribute (
  void * jarg1
  ,
  
  unsigned int jarg2
  )
{
  int jresult ;
  lto_module_t arg1 = (lto_module_t) 0 ;
  unsigned int arg2 ;
  lto_symbol_attributes result;
  
  arg1 = (lto_module_t)jarg1; 
  
  
  arg2 = (unsigned int) jarg2; 
  
  
  result = (lto_symbol_attributes)lto_module_get_symbol_attribute(arg1,arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport void * SWIGSTDCALL Ada_lto_codegen_create (
  )
{
  void * jresult ;
  lto_code_gen_t result;
  
  result = (lto_code_gen_t)lto_codegen_create();
  jresult = (void *) result;      
  
  
  
  return jresult;
  
}



DllExport void SWIGSTDCALL Ada_lto_codegen_dispose (
  void * jarg1
  )
{
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  lto_codegen_dispose(arg1);
  
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_codegen_add_module (
  void * jarg1
  ,
  
  void * jarg2
  )
{
  unsigned int jresult ;
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  lto_module_t arg2 = (lto_module_t) 0 ;
  bool result;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = (lto_module_t)jarg2; 
  
  result = (bool)lto_codegen_add_module(arg1,arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_codegen_set_debug_model (
  void * jarg1
  ,
  
  int jarg2
  )
{
  unsigned int jresult ;
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  lto_debug_model arg2 ;
  bool result;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = (lto_debug_model) jarg2; 
  
  result = (bool)lto_codegen_set_debug_model(arg1,arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_codegen_set_pic_model (
  void * jarg1
  ,
  
  int jarg2
  )
{
  unsigned int jresult ;
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  lto_codegen_model arg2 ;
  bool result;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = (lto_codegen_model) jarg2; 
  
  result = (bool)lto_codegen_set_pic_model(arg1,arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport void SWIGSTDCALL Ada_lto_codegen_set_gcc_path (
  void * jarg1
  ,
  
  char * jarg2
  )
{
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  char *arg2 = (char *) 0 ;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = jarg2; 
  
  lto_codegen_set_gcc_path(arg1,(char const *)arg2);
  
  
}



DllExport void SWIGSTDCALL Ada_lto_codegen_set_assembler_path (
  void * jarg1
  ,
  
  char * jarg2
  )
{
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  char *arg2 = (char *) 0 ;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = jarg2; 
  
  lto_codegen_set_assembler_path(arg1,(char const *)arg2);
  
  
}



DllExport void SWIGSTDCALL Ada_lto_codegen_add_must_preserve_symbol (
  void * jarg1
  ,
  
  char * jarg2
  )
{
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  char *arg2 = (char *) 0 ;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = jarg2; 
  
  lto_codegen_add_must_preserve_symbol(arg1,(char const *)arg2);
  
  
}



DllExport unsigned int SWIGSTDCALL Ada_lto_codegen_write_merged_modules (
  void * jarg1
  ,
  
  char * jarg2
  )
{
  unsigned int jresult ;
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  char *arg2 = (char *) 0 ;
  bool result;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = jarg2; 
  
  result = (bool)lto_codegen_write_merged_modules(arg1,(char const *)arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport void* SWIGSTDCALL Ada_lto_codegen_compile (
  void * jarg1
  ,
  
  size_t* jarg2
  )
{
  void* jresult ;
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  size_t *arg2 = (size_t *) 0 ;
  void *result = 0 ;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  
  arg2 = (size_t *) jarg2;
  
  
  result = (void *)lto_codegen_compile(arg1,arg2);
  jresult = (void *) result;      
  
  
  
  return jresult;
  
}



DllExport void SWIGSTDCALL Ada_lto_codegen_debug_options (
  void * jarg1
  ,
  
  char * jarg2
  )
{
  lto_code_gen_t arg1 = (lto_code_gen_t) 0 ;
  char *arg2 = (char *) 0 ;
  
  arg1 = (lto_code_gen_t)jarg1; 
  
  arg2 = jarg2; 
  
  lto_codegen_debug_options(arg1,(char const *)arg2);
  
  
}



DllExport void* SWIGSTDCALL Ada_llvm_create_optimizer (
  )
{
  void* jresult ;
  llvm_lto_t result;
  
  result = (llvm_lto_t)llvm_create_optimizer();
  jresult = (void *) result;      
  
  
  
  return jresult;
  
}



DllExport void SWIGSTDCALL Ada_llvm_destroy_optimizer (
  void* jarg1
  )
{
  llvm_lto_t arg1 = (llvm_lto_t) 0 ;
  
  arg1 = (llvm_lto_t)jarg1; 
  
  llvm_destroy_optimizer(arg1);
  
  
}



DllExport int SWIGSTDCALL Ada_llvm_read_object_file (
  void* jarg1
  ,
  
  char * jarg2
  )
{
  int jresult ;
  llvm_lto_t arg1 = (llvm_lto_t) 0 ;
  char *arg2 = (char *) 0 ;
  llvm_lto_status_t result;
  
  arg1 = (llvm_lto_t)jarg1; 
  
  arg2 = jarg2; 
  
  result = (llvm_lto_status_t)llvm_read_object_file(arg1,(char const *)arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



DllExport int SWIGSTDCALL Ada_llvm_optimize_modules (
  void* jarg1
  ,
  
  char * jarg2
  )
{
  int jresult ;
  llvm_lto_t arg1 = (llvm_lto_t) 0 ;
  char *arg2 = (char *) 0 ;
  llvm_lto_status_t result;
  
  arg1 = (llvm_lto_t)jarg1; 
  
  arg2 = jarg2; 
  
  result = (llvm_lto_status_t)llvm_optimize_modules(arg1,(char const *)arg2);
  jresult = result; 
  
  
  
  return jresult;
  
}



#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __cplusplus
}
#endif

