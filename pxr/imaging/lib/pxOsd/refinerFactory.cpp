//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
///
/// \file pxOsd/refinerFactory.cpp
///

#include "pxr/imaging/pxOsd/refinerFactory.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/base/tf/diagnostic.h"

#include <opensubdiv/far/topologyRefinerFactory.h>

#include <boost/bind.hpp>

namespace {

struct Converter {

    Converter(PxOsdMeshTopology const & topo,
              std::vector<VtIntArray> const &fvarTopologies, TfToken t) :
        name(t), topology(topo), fvarTopologies(fvarTopologies) { }

    OpenSubdiv::Sdc::SchemeType GetType() const;

    OpenSubdiv::Sdc::Options GetOptions() const;

    TfToken name;
    PxOsdMeshTopology const & topology;
    std::vector<VtIntArray> const &fvarTopologies;
};

OpenSubdiv::Sdc::SchemeType
Converter::GetType() const {

    using namespace OpenSubdiv::Sdc;

    TfToken const scheme = topology.GetScheme();

    SchemeType type = SCHEME_CATMARK;
    if (scheme==PxOsdOpenSubdivTokens->catmark
                              or scheme==PxOsdOpenSubdivTokens->catmullClark) {
        type = SCHEME_CATMARK;
    } else if (scheme==PxOsdOpenSubdivTokens->loop) {
        type = SCHEME_LOOP;
        // in loop case, all input faces have to be triangle.
        int numFaces = topology.GetFaceVertexCounts().size();
        int const * numVertsPtr = topology.GetFaceVertexCounts().cdata();
        if (std::find_if(numVertsPtr, numVertsPtr + numFaces,
                         boost::bind(std::not_equal_to<int>(), _1, 3))
            == numVertsPtr + numFaces) {
        } else {
            TF_WARN("Can't apply loop subdivision on prim %s, since "
                    "it has non-triangle face(s).", name.GetText());
        }
    } else if (scheme==PxOsdOpenSubdivTokens->bilinear) {
        type = SCHEME_BILINEAR;
    } else {
        TF_WARN("Unsupported scheme (%s) (%s)",
            scheme.GetText(), name.GetText());
    }
    return type;
}

OpenSubdiv::Sdc::Options
Converter::GetOptions() const {
    using namespace OpenSubdiv::Sdc;

    Options options;

    //
    // vertex boundary interpolation rule
    //

    // XXX: there is a bug in OpenSubdiv 3.0.0, which drops
    // boundary faces of bilinear scheme mesh when
    // boundaryInterpolationMode=None. To workaround the bug
    // override boundary interpolation mode to be edgeAndCorner.
    TfToken const scheme = topology.GetScheme();
    TfToken const interpolateBoundary =
        scheme == PxOsdOpenSubdivTokens->bilinear ?
        PxOsdOpenSubdivTokens->edgeAndCorner :
        topology.GetSubdivTags().GetVertexInterpolationRule();

    if (not interpolateBoundary.IsEmpty()) {
               if (interpolateBoundary==PxOsdOpenSubdivTokens->none) {
            options.SetVtxBoundaryInterpolation(Options::VTX_BOUNDARY_NONE);
        } else if (interpolateBoundary==PxOsdOpenSubdivTokens->edgeOnly) {
            options.SetVtxBoundaryInterpolation(Options::VTX_BOUNDARY_EDGE_ONLY);
        } else if (interpolateBoundary==PxOsdOpenSubdivTokens->edgeAndCorner) {
            options.SetVtxBoundaryInterpolation(Options::VTX_BOUNDARY_EDGE_AND_CORNER);
        } else {
            TF_WARN("Unknown vertex boundary interpolation rule (%s) (%s)",
                interpolateBoundary.GetText(), name.GetText());
        }
    } else {
        // XXX legacy assets expect a default of "edge & corner" if no
        //     tag has been defined. this should default to Osd defaults
        //     instead
        options.SetVtxBoundaryInterpolation(Options::VTX_BOUNDARY_EDGE_AND_CORNER);
    }

    //
    // face-varying boundary interpolation rule
    //

    TfToken const faceVaryingLinearInterpolation =
        topology.GetSubdivTags().GetFaceVaryingInterpolationRule();

    if (not faceVaryingLinearInterpolation.IsEmpty()) {
        if (faceVaryingLinearInterpolation==PxOsdOpenSubdivTokens->all) {
            options.SetFVarLinearInterpolation(Options::FVAR_LINEAR_ALL);
        } else if (faceVaryingLinearInterpolation==PxOsdOpenSubdivTokens->cornersPlus1) {
            options.SetFVarLinearInterpolation(Options::FVAR_LINEAR_CORNERS_PLUS1);
        } else if (faceVaryingLinearInterpolation==PxOsdOpenSubdivTokens->none) {
            options.SetFVarLinearInterpolation(Options::FVAR_LINEAR_NONE);
        } else if (faceVaryingLinearInterpolation==PxOsdOpenSubdivTokens->boundaries) {
            options.SetFVarLinearInterpolation(Options::FVAR_LINEAR_BOUNDARIES);
        } else {
            TF_WARN("Unknown face-varying boundary interpolation rule (%s) (%s)",
                faceVaryingLinearInterpolation.GetText(), name.GetText());
        }
    } else {
        // XXX legacy assets expect a default of "edge & corner" if no
        //     tag has been defined. this should default to Osd defaults
        //     instead
        options.SetFVarLinearInterpolation(Options::FVAR_LINEAR_NONE);
    }

    //
    // creasing method
    //

    TfToken const creaseMethod =
        topology.GetSubdivTags().GetCreaseMethod();

    if (not creaseMethod.IsEmpty()) {
               if (creaseMethod==PxOsdOpenSubdivTokens->uniform) {
            options.SetCreasingMethod(Options::CREASE_UNIFORM);
        } else if (creaseMethod==PxOsdOpenSubdivTokens->chaikin) {
            options.SetCreasingMethod(Options::CREASE_CHAIKIN);
        } else {
            TF_WARN("Unkown creasing method (%s) (%s)",
                creaseMethod.GetText(), name.GetText());
        }
    }

    //
    // triangle subdivision
    //

    TfToken const trianglesSubdivision =
        topology.GetSubdivTags().GetTriangleSubdivision();

    if (not trianglesSubdivision.IsEmpty()) {
        if (trianglesSubdivision==PxOsdOpenSubdivTokens->catmark or
                trianglesSubdivision==PxOsdOpenSubdivTokens->catmullClark) {
            options.SetTriangleSubdivision(Options::TRI_SUB_CATMARK);
        } else if (trianglesSubdivision==PxOsdOpenSubdivTokens->smooth) {
            options.SetTriangleSubdivision(Options::TRI_SUB_SMOOTH);
        } else {
            TF_WARN("Unknown triangle subdivision rule (%s) (%s)",
                trianglesSubdivision.GetText(), name.GetText());
        }
    }

    return options;
}

}

// OpenSubdiv 3.x API requires that the client code provides
// template specialization for topology annotations.

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Far {

//----------------------------------------------------------
template <> inline bool
TopologyRefinerFactory<Converter>::resizeComponentTopology(
    Far::TopologyRefiner & refiner, Converter const & converter) {

    PxOsdMeshTopology const topology = converter.topology;

    int numFaces = topology.GetFaceVertexCounts().size();
    int maxVertIndex = 0;

    int const * vertCounts = topology.GetFaceVertexCounts().cdata(),
              * vertIndices = topology.GetFaceVertexIndices().cdata();

    setNumBaseFaces(refiner, numFaces);

    for (int face=0; face<numFaces; ++face) {

        int nverts = vertCounts[face];
        setNumBaseFaceVertices(refiner, face, nverts);

        for (int vert=0; vert<nverts; ++vert) {
            maxVertIndex = std::max(maxVertIndex, vertIndices[vert]);
        }
        vertIndices+=nverts;
    }
    setNumBaseVertices(refiner, maxVertIndex+1);

    return true;
}

//----------------------------------------------------------
template <>
inline bool
TopologyRefinerFactory<Converter>::assignComponentTopology(
    Far::TopologyRefiner & refiner, Converter const & converter) {

    PxOsdMeshTopology const topology = converter.topology;

    int const * vertIndices = topology.GetFaceVertexIndices().cdata();

    bool flip = (topology.GetOrientation() != PxOsdOpenSubdivTokens->rightHanded);

    for (int face=0, idx=0; face<refiner.GetLevel(0).GetNumFaces(); ++face) {

        IndexArray dstFaceVerts = getBaseFaceVertices(refiner, face);

        if (flip) {
            dstFaceVerts[0] = vertIndices[idx++];
            for (int vert=dstFaceVerts.size()-1; vert > 0; --vert) {
                dstFaceVerts[vert] = vertIndices[idx++];
            }
        } else {
            for (int vert=0; vert<dstFaceVerts.size(); ++vert) {

                dstFaceVerts[vert] = vertIndices[idx++];
            }
        }
    }

    return true;
}

//----------------------------------------------------------

template <>
inline bool
TopologyRefinerFactory<Converter>::assignComponentTags(
    Far::TopologyRefiner & refiner, Converter const & converter) {

    PxOsdMeshTopology const & topology = converter.topology;

    PxOsdSubdivTags const & tags = topology.GetSubdivTags();

    //
    // creases
    //

    // The sharpnesses can be defined either per-crease or per-edge.
    VtIntArray const creaseIndices = tags.GetCreaseIndices(),
                     creaseLengths = tags.GetCreaseLengths();
    VtFloatArray const creaseWeights = tags.GetCreaseWeights();

    size_t numCreaseSets = creaseLengths.size();
    bool perEdgeCrease = creaseWeights.size() != numCreaseSets;

    if (perEdgeCrease) {
        // validate per-edge crease.
        int numEdges = 0;
        for (size_t i = 0; i < numCreaseSets; ++i) {
            numEdges += creaseLengths[i] - 1;
        }
        if (creaseWeights.size() != static_cast<size_t>(numEdges)) {
            TF_WARN("Invalid length of crease sharpnesses (%s)\n",
                converter.name.GetText());
            numCreaseSets = 0;
        }
    }
    for (size_t i=0, cindex=0, sindex=0; i < numCreaseSets; ++i) {

        int numSegments = creaseLengths[i] - 1;

        for (int j = 0; j < numSegments; ++j) {
            int v0 = creaseIndices[cindex+j],
                v1 = creaseIndices[cindex+j+1];

            OpenSubdiv::Vtr::Index edge = refiner.GetLevel(0).FindEdge(v0, v1);
            if (edge==OpenSubdiv::Vtr::INDEX_INVALID) {
                TF_WARN("Set edge sharpness cannot find edge (%d-%d) (%s)",
                        v0, v1, converter.name.GetText());
            } else {
                setBaseEdgeSharpness(refiner,
                    edge, std::max(0.0f, creaseWeights[sindex]));
            }

            if (perEdgeCrease) ++sindex;
        }
        if (not perEdgeCrease) ++sindex;
        cindex += creaseLengths[i];
    }

    //
    // corners
    //

    VtIntArray const cornerIndices = tags.GetCornerIndices();
    VtFloatArray const cornerWeights = tags.GetCornerWeights();

    size_t numCorners = cornerIndices.size();

    if (cornerWeights.size() != numCorners) {
        TF_WARN("Invalid length of corner sharpnesses at prim %s\n",
            converter.name.GetText());
        numCorners = 0;
    }
    for (size_t i=0; i < numCorners; ++i) {
        int vert = cornerIndices[i];
        if (vert >= 0 and vert < refiner.GetLevel(0).GetNumVertices()) {
            setBaseVertexSharpness(refiner,
                vert, std::max(0.0f, cornerWeights[i]));
        } else {
            TF_WARN("Set vertex sharpness cannot find vertex (%d) (%s)",
                vert, converter.name.GetText());
        }
    }

    //
    // holes
    //

    VtIntArray const holeIndices = tags.GetHoleIndices();

    int numHoles = holeIndices.size();

    for (int i=0; i < numHoles; ++i) {
        int face = holeIndices[i];
        if (face >= 0 and face < refiner.GetLevel(0).GetNumFaces()) {
            setBaseFaceHole(refiner, face, true);
        } else {
            TF_WARN("Set hole cannot find face (%d) (%s)",
                face, converter.name.GetText());
        }
    }

    return true;
}

//----------------------------------------------------------

template <>
bool
TopologyRefinerFactory<Converter>::assignFaceVaryingTopology(
    TopologyRefiner & refiner, Converter const & converter) {

    if (converter.fvarTopologies.empty()) return true;

    TF_FOR_ALL (it, converter.fvarTopologies) {
        VtIntArray const &fvIndices = *it;

        // find fvardata size
        int maxIndex = -1;
        for (size_t i = 0; i < fvIndices.size(); ++i) {
            maxIndex = std::max(maxIndex, fvIndices[i]);
        }

        size_t nfaces = getNumBaseFaces(refiner);
        size_t channel = createBaseFVarChannel(refiner, maxIndex+1);

        bool flip = (converter.topology.GetOrientation() !=
                     PxOsdOpenSubdivTokens->rightHanded);

        for (size_t i=0, ofs=0; i < nfaces; ++i) {
            Far::IndexArray faceIndices = getBaseFaceFVarValues(refiner, i, channel);
            size_t numVerts = faceIndices.size();

            if (not TF_VERIFY(ofs + numVerts <= fvIndices.size())) {
                return false;
            }

            if (flip) {
                faceIndices[0] = fvIndices[ofs++];
                for (int j = numVerts-1; j > 0; --j) {
                    faceIndices[j] = fvIndices[ofs++];
                }
            } else {
                for (size_t j = 0; j < numVerts; ++j) {
                    faceIndices[j] = fvIndices[ofs++];
                }
            }
        }
    }

    return true;
}

//----------------------------------------------------------
template <>
inline void
TopologyRefinerFactory<Converter>::reportInvalidTopology(
    TopologyRefinerFactory::TopologyError /* errCode */,
        char const * msg, Converter const & converter) {
    TF_WARN("%s (%s)", msg, converter.name.GetText());
}

// ---------------------------------------------------------------------------

} // namespace Far

} // namespace OPENSUBDIV_VERSION
} // namespace OpenSubdiv

// ---------------------------------------------------------------------------
PxOsdTopologyRefinerSharedPtr
PxOsdRefinerFactory::Create(
    PxOsdMeshTopology const & topology, TfToken name) {

    std::vector<VtIntArray> fvarTopologies;
    Converter converter(topology, fvarTopologies, name);

    OpenSubdiv::Far::TopologyRefinerFactory<Converter>::Options
        options(converter.GetType(), converter.GetOptions());

    OpenSubdiv::Far::TopologyRefiner * refiner =
        OpenSubdiv::Far::TopologyRefinerFactory<Converter>::Create(
            converter, options);

    return PxOsdTopologyRefinerSharedPtr(refiner);
}


PxOsdTopologyRefinerSharedPtr
PxOsdRefinerFactory::Create(
    PxOsdMeshTopology const & topology,
    std::vector<VtIntArray> const &fvarTopologies,
    TfToken name) {

    Converter converter(topology, fvarTopologies, name);

    OpenSubdiv::Far::TopologyRefinerFactory<Converter>::Options
        options(converter.GetType(), converter.GetOptions());

    OpenSubdiv::Far::TopologyRefiner * refiner =
        OpenSubdiv::Far::TopologyRefinerFactory<Converter>::Create(
            converter, options);

    return PxOsdTopologyRefinerSharedPtr(refiner);
}


