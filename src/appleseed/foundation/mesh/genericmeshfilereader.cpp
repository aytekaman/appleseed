
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2012 Francois Beaune, Jupiter Jazz Limited
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// Interface header.
#include "genericmeshfilereader.h"

// appleseed.foundation headers.
#include "foundation/core/exceptions/exceptionunsupportedfileformat.h"
#include "foundation/mesh/alembicmeshfilereader.h"
#include "foundation/utility/string.h"

// boost headers.
#include "boost/filesystem/path.hpp"

using namespace boost;
using namespace std;

namespace foundation
{

GenericMeshFileReader::GenericMeshFileReader(const string& filename)
  : m_filename(filename)
  , m_obj_options(OBJMeshFileReader::Default)
{
}

int GenericMeshFileReader::get_obj_options() const
{
    return m_obj_options;
}

void GenericMeshFileReader::set_obj_options(const int obj_options)
{
    m_obj_options = obj_options;
}

void GenericMeshFileReader::read(IMeshBuilder& builder)
{
    const filesystem::path filepath(m_filename);
    const string extension = lower_case(filepath.extension().string());

    if (extension == ".obj")
    {
        OBJMeshFileReader reader(m_filename, m_obj_options);
        reader.read(builder);
    }
    else if (extension == ".abc")
    {
        AlembicMeshFileReader reader(m_filename);
        reader.read(builder);
    }
    else
    {
        throw ExceptionUnsupportedFileFormat(m_filename.c_str());
    }
}

}   // namespace foundation
