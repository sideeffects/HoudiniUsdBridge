//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_API_H
#define USDHOUDINI_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDHOUDINI_API
#   define USDHOUDINI_API_TEMPLATE_CLASS(...)
#   define USDHOUDINI_API_TEMPLATE_STRUCT(...)
#   define USDHOUDINI_LOCAL
#else
#   if defined(USDHOUDINI_EXPORTS)
#       define USDHOUDINI_API ARCH_EXPORT
#       define USDHOUDINI_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDHOUDINI_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDHOUDINI_API ARCH_IMPORT
#       define USDHOUDINI_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDHOUDINI_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDHOUDINI_LOCAL ARCH_HIDDEN
#endif

#endif
