/*
 * Copyright 2019 Side Effects Software Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __HUSD_API_h__
#define __HUSD_API_h__

#include <SYS/SYS_Visibility.h>

#ifndef BOOST_NS
#define BOOST_NS hboost
#endif
#define BOOST_HEADER(HEADER_FILE) <BOOST_NS/HEADER_FILE>

#ifdef HUSD_EXPORTS
#define HUSD_API SYS_VISIBILITY_EXPORT
#define HUSD_API_TMPL SYS_VISIBILITY_EXPORT_TMPL
#define HUSD_API_TINST SYS_VISIBILITY_EXPORT_TINST
#else
#define HUSD_API SYS_VISIBILITY_IMPORT
#define HUSD_API_TMPL SYS_VISIBILITY_IMPORT_TMPL
#define HUSD_API_TINST SYS_VISIBILITY_IMPORT_TINST
#endif

#endif
