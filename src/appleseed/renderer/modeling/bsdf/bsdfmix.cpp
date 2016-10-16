
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
// Copyright (c) 2014-2016 Francois Beaune, The appleseedhq Organization
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
#include "bsdfmix.h"

// appleseed.renderer headers.
#include "renderer/global/globallogger.h"
#include "renderer/global/globaltypes.h"
#include "renderer/modeling/bsdf/bsdf.h"
#include "renderer/modeling/bsdf/bsdfwrapper.h"
#include "renderer/modeling/scene/assembly.h"

// appleseed.foundation headers.
#include "foundation/math/basis.h"
#include "foundation/math/vector.h"
#include "foundation/platform/types.h"
#include "foundation/utility/api/apistring.h"
#include "foundation/utility/api/specializedapiarrays.h"
#include "foundation/utility/containers/dictionary.h"

// Standard headers.
#include <cassert>
#include <cstddef>
#include <string>

// Forward declarations.
namespace foundation    { class IAbortSwitch; }
namespace renderer      { class InputEvaluator; }
namespace renderer      { class ShadingPoint; }

using namespace foundation;
using namespace std;

namespace renderer
{

namespace
{
    //
    // A mix of two BSDFs, each with its own weight.
    //

    const char* Model = "bsdf_mix";

    class BSDFMixImpl
      : public BSDF
    {
      public:
        BSDFMixImpl(
            const char*             name,
            const ParamArray&       params)
          : BSDF(name, Reflective, ScatteringMode::All, params)
        {
            m_inputs.declare("weight0", InputFormatScalar);
            m_inputs.declare("weight1", InputFormatScalar);
        }

        virtual void release() APPLESEED_OVERRIDE
        {
            delete this;
        }

        virtual const char* get_model() const APPLESEED_OVERRIDE
        {
            return Model;
        }

        virtual bool on_frame_begin(
            const Project&          project,
            const BaseGroup*        parent,
            OnFrameBeginRecorder&   recorder,
            IAbortSwitch*           abort_switch) APPLESEED_OVERRIDE
        {
            if (!BSDF::on_frame_begin(project, parent, recorder, abort_switch))
                return false;

            const Assembly* assembly = static_cast<const Assembly*>(parent);

            m_bsdf[0] = retrieve_bsdf(*assembly, "bsdf0");
            m_bsdf[1] = retrieve_bsdf(*assembly, "bsdf1");

            if (m_bsdf[0] == 0 || m_bsdf[1] == 0)
                return false;

            m_bsdf_data_offset[0] = get_inputs().compute_data_size();
            m_bsdf_data_offset[1] = m_bsdf_data_offset[0] + m_bsdf[0]->compute_input_data_size(*assembly);

            return true;
        }

        virtual size_t compute_input_data_size(
            const Assembly&         assembly) const APPLESEED_OVERRIDE
        {
            size_t size = get_inputs().compute_data_size();

            {
                const BSDF* bsdf0 = retrieve_bsdf(assembly, "bsdf0");
                if (bsdf0)
                    size += bsdf0->compute_input_data_size(assembly);
            }

            {
                const BSDF* bsdf1 = retrieve_bsdf(assembly, "bsdf1");
                if (bsdf1)
                    size += bsdf1->compute_input_data_size(assembly);
            }

            return size;
        }

        virtual void evaluate_inputs(
            const ShadingContext&   shading_context,
            InputEvaluator&         input_evaluator,
            const ShadingPoint&     shading_point,
            const size_t            offset) const APPLESEED_OVERRIDE
        {
            assert(m_bsdf[0] && m_bsdf[1]);

            BSDF::evaluate_inputs(
                shading_context,
                input_evaluator,
                shading_point,
                offset);

            m_bsdf[0]->evaluate_inputs(
                shading_context,
                input_evaluator,
                shading_point,
                offset + m_bsdf_data_offset[0]);

            m_bsdf[1]->evaluate_inputs(
                shading_context,
                input_evaluator,
                shading_point,
                offset + m_bsdf_data_offset[1]);
        }

        APPLESEED_FORCE_INLINE virtual void sample(
            SamplingContext&        sampling_context,
            const void*             data,
            const bool              adjoint,
            const bool              cosine_mult,
            BSDFSample&             sample) const APPLESEED_OVERRIDE
        {
            assert(m_bsdf[0] && m_bsdf[1]);

            // Retrieve the blending weights.
            const InputValues* values = static_cast<const InputValues*>(data);
            const float w[2] = { static_cast<float>(values->m_weight[0]), static_cast<float>(values->m_weight[1]) };

            // Handle absorption.
            const float total_weight = w[0] + w[1];
            if (total_weight == 0.0f)
                return;

            // Choose which of the two BSDFs to sample.
            sampling_context.split_in_place(1, 1);
            const double s = sampling_context.next_double2();
            const size_t bsdf_index = s * total_weight < w[0] ? 0 : 1;

            // Sample the chosen BSDF.
            m_bsdf[bsdf_index]->sample(
                sampling_context,
                get_bsdf_data(data, bsdf_index),
                adjoint,
                false,                      // do not multiply by |cos(incoming, normal)|
                sample);
        }

        APPLESEED_FORCE_INLINE virtual float evaluate(
            const void*             data,
            const bool              adjoint,
            const bool              cosine_mult,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector3f&         outgoing,
            const Vector3f&         incoming,
            const int               modes,
            Spectrum&               value) const APPLESEED_OVERRIDE
        {
            assert(m_bsdf[0] && m_bsdf[1]);

            // Retrieve the blending weights.
            const InputValues* values = static_cast<const InputValues*>(data);
            float w0 = static_cast<float>(values->m_weight[0]);
            float w1 = static_cast<float>(values->m_weight[1]);
            const float total_weight = w0 + w1;

            // Handle absorption.
            if (total_weight == 0.0f)
                return 0.0f;

            // Normalize the blending weights.
            const float rcp_total_weight = 1.0f / total_weight;
            w0 *= rcp_total_weight;
            w1 *= rcp_total_weight;

            // Evaluate the first BSDF.
            Spectrum bsdf0_value;
            const float bsdf0_prob =
                w0 > 0.0
                    ? m_bsdf[0]->evaluate(
                        get_bsdf_data(data, 0),
                        adjoint,
                        false,                  // do not multiply by |cos(incoming, normal)|
                        geometric_normal,
                        shading_basis,
                        outgoing,
                        incoming,
                        modes,
                        bsdf0_value)
                    : 0.0f;

            // Evaluate the second BSDF.
            Spectrum bsdf1_value;
            const float bsdf1_prob =
                w1 > 0.0
                    ? m_bsdf[1]->evaluate(
                          get_bsdf_data(data, 1),
                          adjoint,
                          false,                // do not multiply by |cos(incoming, normal)|
                          geometric_normal,
                          shading_basis,
                          outgoing,
                          incoming,
                          modes,
                          bsdf1_value)
                    : 0.0f;

            // Blend BSDF values.
            value.set(0.0f);
            if (bsdf0_prob > 0.0f)
                madd(value, bsdf0_value, static_cast<float>(w0));
            if (bsdf1_prob > 0.0f)
                madd(value, bsdf1_value, static_cast<float>(w1));

            // Blend PDF values.
            return bsdf0_prob * w0 + bsdf1_prob * w1;
        }

        APPLESEED_FORCE_INLINE virtual float evaluate_pdf(
            const void*             data,
            const Vector3f&         geometric_normal,
            const Basis3f&          shading_basis,
            const Vector3f&         outgoing,
            const Vector3f&         incoming,
            const int               modes) const APPLESEED_OVERRIDE
        {
            assert(m_bsdf[0] && m_bsdf[1]);

            // Retrieve the blending weights.
            const InputValues* values = static_cast<const InputValues*>(data);
            const float w0 = static_cast<float>(values->m_weight[0]);
            const float w1 = static_cast<float>(values->m_weight[1]);
            const float total_weight = w0 + w1;

            // Handle absorption.
            if (total_weight == 0.0f)
                return 0.0f;

            // Evaluate the PDF of the first BSDF.
            const float bsdf0_prob =
                w0 > 0.0
                    ? m_bsdf[0]->evaluate_pdf(
                          get_bsdf_data(data, 0),
                          geometric_normal,
                          shading_basis,
                          outgoing,
                          incoming,
                          modes)
                    : 0.0f;

            // Evaluate the PDF of the second BSDF.
            const float bsdf1_prob =
                w1 > 0.0
                    ? m_bsdf[1]->evaluate_pdf(
                          get_bsdf_data(data, 1),
                          geometric_normal,
                          shading_basis,
                          outgoing,
                          incoming,
                          modes)
                    : 0.0f;

            // Blend PDF values.
            return (bsdf0_prob * w0 + bsdf1_prob * w1) / total_weight;
        }

      private:
        APPLESEED_DECLARE_INPUT_VALUES(InputValues)
        {
            ScalarInput  m_weight[2];
        };

        const BSDF* m_bsdf[2];
        size_t      m_bsdf_data_offset[2];

        const BSDF* retrieve_bsdf(const Assembly& assembly, const char* param_name) const
        {
            const string bsdf_name = m_params.get_required<string>(param_name, "");
            if (bsdf_name.empty())
            {
                RENDERER_LOG_ERROR("while preparing bsdf \"%s\": no bsdf bound to \"%s\".", get_path().c_str(), param_name);
                return 0;
            }

            const BSDF* bsdf = assembly.bsdfs().get_by_name(bsdf_name.c_str());
            if (bsdf == 0)
                RENDERER_LOG_ERROR("while preparing bsdf \"%s\": cannot find bsdf \"%s\".", get_path().c_str(), bsdf_name.c_str());

            return bsdf;
        }

        const void* get_bsdf_data(const void* data, const size_t bsdf_index) const
        {
            assert(bsdf_index < 2);
            return static_cast<const uint8*>(data) + m_bsdf_data_offset[bsdf_index];
        }
    };

    typedef BSDFWrapper<BSDFMixImpl> BSDFMix;
}


//
// BSDFMixFactory class implementation.
//

const char* BSDFMixFactory::get_model() const
{
    return Model;
}

Dictionary BSDFMixFactory::get_model_metadata() const
{
    return
        Dictionary()
            .insert("name", Model)
            .insert("label", "BSDF Mix");
}

DictionaryArray BSDFMixFactory::get_input_metadata() const
{
    DictionaryArray metadata;

    metadata.push_back(
        Dictionary()
            .insert("name", "bsdf0")
            .insert("label", "BSDF 1")
            .insert("type", "entity")
            .insert("entity_types",
                Dictionary().insert("bsdf", "BSDF"))
            .insert("use", "required"));

    metadata.push_back(
        Dictionary()
            .insert("name", "weight0")
            .insert("label", "Weight 1")
            .insert("type", "colormap")
            .insert("entity_types",
                Dictionary().insert("texture_instance", "Textures"))
            .insert("use", "required")
            .insert("default", "0.5"));

    metadata.push_back(
        Dictionary()
            .insert("name", "bsdf1")
            .insert("label", "BSDF 2")
            .insert("type", "entity")
            .insert("entity_types",
                Dictionary().insert("bsdf", "BSDF"))
            .insert("use", "required"));

    metadata.push_back(
        Dictionary()
            .insert("name", "weight1")
            .insert("label", "Weight 2")
            .insert("type", "colormap")
            .insert("entity_types",
                Dictionary().insert("texture_instance", "Textures"))
            .insert("use", "required")
            .insert("default", "0.5"));

    return metadata;
}

auto_release_ptr<BSDF> BSDFMixFactory::create(
    const char*         name,
    const ParamArray&   params) const
{
    return auto_release_ptr<BSDF>(new BSDFMix(name, params));
}

auto_release_ptr<BSDF> BSDFMixFactory::static_create(
    const char*         name,
    const ParamArray&   params)
{
    return auto_release_ptr<BSDF>(new BSDFMix(name, params));
}

}   // namespace renderer
