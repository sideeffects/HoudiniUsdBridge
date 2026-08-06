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
 *
 * Produced by:
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#pragma once

/// Options are created by parsing the value of HUSD_HDEBUG_DELEGATE
///
/// The string provided by HUSD_DEBUG_DELEGATE is parsed as a comma separated
/// string.  Boolean options can be enabled by "trace=1", "trace", "trace=on".
/// They can be disabled by "trace=0", "trace=off", "no-trace".
/// Options are:
///     - *, all:       Enable all tracing
///     - "help"        Print help (one time)
///     - bool trace:   Log function calls
///     - bool memory:  Track memory usage
///     - bool object:  Track object counts
///     - bool image:   Print image/projection information
namespace HDOptions
{
    bool        trace();        // Trace functions (false)
    bool        memory();       // Track memory (true)
    bool        object();       // Count objects (true)
    bool        image();        // Image information (true)
    bool        store();        // Hold onto geometry (true)
    bool        projection();   // Print out Hydra projection matrix (false)
    bool        geo();          // Dump out geometry when syncing
    bool        material();     // Print material assignments
    bool        bprim();        // Print bprim creation/destruction
    bool        rprim();        // Print rprim creation/destruction
    bool        sprim();        // Print sprim creation/destruction
    float       sleep();        // Sleep for N seconds at end of render
};
