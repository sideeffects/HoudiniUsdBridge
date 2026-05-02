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

// This header exists to keep the RE headers separate from teh GLEW headers,
// which both define their own wrappers around all the GL extension functions.

class RE_Wrapper
{
public:
                 RE_Wrapper(bool createcontext);
                ~RE_Wrapper();

    bool         isOpenGLAvailable();

private:
    bool	 mySetContext;
};

