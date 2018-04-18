
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
// Copyright (c) 2014-2017 Francois Beaune, The appleseedhq Organization
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
#include "tilejob.h"

// appleseed.renderer headers.
#include "renderer/global/globallogger.h"
#include "renderer/kernel/rendering/ishadingresultframebufferfactory.h"
#include "renderer/kernel/rendering/shadingresultframebuffer.h"
#include "renderer/kernel/rendering/itilecallback.h"
#include "renderer/kernel/rendering/itilerenderer.h"
#include "renderer/modeling/frame/frame.h"

// appleseed.foundation headers.
#include "foundation/image/canvasproperties.h"
#include "foundation/image/image.h"
#include "foundation/image/tile.h"

// Standard headers.
#include <cassert>
#include <exception>

using namespace foundation;
using namespace std;

namespace renderer
{

//
// TileJob class implementation.
//

TileJob::TileJob(
    const TileRendererVector&   tile_renderers,
    const TileCallbackVector&   tile_callbacks,
    IShadingResultFrameBufferFactory&       framebuffer_factory,
    const Frame&                frame,
    const size_t                tile_x,
    const size_t                tile_y,
    const size_t                tile_level,
    const size_t                pass_hash,
    const Spectrum::Mode        spectrum_mode,
    foundation::JobQueue&       job_queue,
    IAbortSwitch&               abort_switch)
  : m_tile_renderers(tile_renderers)
  , m_tile_callbacks(tile_callbacks)
  , m_framebuffer_factory(framebuffer_factory)
  , m_frame(frame)
  , m_tile_x(tile_x)
  , m_tile_y(tile_y)
  , m_tile_level(tile_level)
  , m_pass_hash(pass_hash)
  , m_spectrum_mode(spectrum_mode)
  , m_job_queue(job_queue)
  , m_abort_switch(abort_switch)
{
    // Either there is no tile callback, or there is the same number
    // of tile callbacks and rendering threads.
    assert(
           m_tile_callbacks.size() == 0
        || m_tile_callbacks.size() == tile_renderers.size());
}

void TileJob::execute(const size_t thread_index)
{
    //if (m_tile_renderers.size() > m_job_queue.get_scheduled_job_count())
        //RENDERER_LOG_INFO("split");
    
    const bool split = (m_tile_level == 0) && m_tile_renderers.size() > m_job_queue.get_scheduled_job_count() / 2;
    AABB2u box;

    if (split)
    {
        // Here split both image tiles and frame buffer tiles.
        m_frame.image().tile(m_tile_x, m_tile_y, m_tile_level).split();
        m_framebuffer_factory.create(m_frame, m_tile_x, m_tile_y, m_tile_level, box)->split_buffer();

        for (int i = 0; i < 2; ++i)
        {
            for (int j = 0; j < 2; ++j)
            {
                m_job_queue.schedule(new TileJob(
                    m_tile_renderers,
                    m_tile_callbacks,
                    m_framebuffer_factory,
                    m_frame,
                    2 * m_tile_x + i,
                    2 * m_tile_y + j,
                    m_tile_level + 1,
                    m_pass_hash,
                    m_spectrum_mode,
                    m_job_queue,
                    m_abort_switch), true);
            }
        }

        return;
    }

    // Make sure that referenced tile is merged
    m_frame.image().tile(m_tile_x, m_tile_y, m_tile_level).combine();
    m_framebuffer_factory.create(m_frame, m_tile_x, m_tile_y, m_tile_level, box)->combine_buffer();

    // Initialize thread-local variables.
    Spectrum::set_mode(m_spectrum_mode);

    // Retrieve the tile callback.
    assert(thread_index < m_tile_renderers.size());
    ITileCallback* tile_callback =
        m_tile_callbacks.size() == m_tile_renderers.size()
            ? m_tile_callbacks[thread_index]
            : nullptr;

    // Call the pre-render tile callback.
    if (tile_callback)
        tile_callback->on_tile_begin(&m_frame, m_tile_x, m_tile_y, m_tile_level);

    try
    {
        // Render the tile.
        m_tile_renderers[thread_index]->render_tile(
            m_frame,
            m_tile_x,
            m_tile_y,
            m_tile_level,
            m_pass_hash,
            m_abort_switch);
    }
    catch (const exception&)
    {
        // Call the post-render tile callback.
        if (tile_callback)
            tile_callback->on_tile_end(&m_frame, m_tile_x, m_tile_y, m_tile_level);

        // Rethrow the exception.
        throw;
    }

    // Call the post-render tile callback.
    if (tile_callback)
        tile_callback->on_tile_end(&m_frame, m_tile_x, m_tile_y, m_tile_level);
}

}   // namespace renderer
