#pragma once
//264
#ifdef __cplusplus
    #if defined(_MSVC_LANG) && _MSVC_LANG > __cplusplus
        #define _STL_LANG _MSVC_LANG
    #else  // ^^^ language mode is _MSVC_LANG / language mode is __cplusplus vvv
        #define _STL_LANG __cplusplus
    #endif // ^^^ language mode is larger of _MSVC_LANG and __cplusplus ^^^
#else  // ^^^ determine compiler's C++ mode / no C++ support vvv
    #define _STL_LANG 0L
#endif // ^^^ no C++ support ^^^

#ifndef _HAS_CXX17
    #if _STL_LANG > 201402L
        #define _HAS_CXX17 1
    #else
        #define _HAS_CXX17 0
    #endif
#endif // _HAS_CXX17

#ifndef _HAS_CXX20
    #if _HAS_CXX17 && _STL_LANG > 201703L
        #define _HAS_CXX20 1
    #else
        #define _HAS_CXX20 0
    #endif
#endif // _HAS_CXX20

#ifndef _HAS_CXX23
    #if _HAS_CXX20 && _STL_LANG > 202002L
        #define _HAS_CXX23 1
    #else
        #define _HAS_CXX23 0
    #endif
#endif // _HAS_CXX23

#undef _STL_LANG
//300
//319
#if _HAS_NODISCARD
    #define _NODISCARD [[nodiscard]]
#else // ^^^ CAN HAZ [[nodiscard]] / NO CAN HAZ [[nodiscard]] vvv
    #define _NODISCARD
#endif // _HAS_NODISCARD
//325