//
// Created by wafemand on 13/02/2022.
//

#ifndef BANDAGE_GRAPH_GFAWRAPPER_HPP_
#define BANDAGE_GRAPH_GFAWRAPPER_HPP_

#include "gfa1/include/gfa.h"

#include <memory>
#include <filesystem>
#include <optional>

namespace gfa {

class Wrapper;

class PathWrapper {
  public:
    explicit PathWrapper(gfa_path_t &gfaPath) : m_gfaPath(gfaPath) {}

    int64_t length() const {
        return m_gfaPath.n_seg;
    }

    int64_t getNthVertexId(int64_t n) const {
        return m_gfaPath.v[n];
    }

    const char *name() const {
        return m_gfaPath.name;
    }

  private:
    gfa_path_t &m_gfaPath;
};


class TagsWrapper {
  public:
    explicit TagsWrapper(gfa_aux_t &gfaAux) : m_gfaAux(gfaAux) {}

    // TODO: add type checking
    template <typename T>
    std::optional<T> getNumberTag(const char *tagName) const {
        auto tag = gfa_aux_get(static_cast<int>(m_gfaAux.l_aux), m_gfaAux.aux, tagName);
        if (tag == nullptr) {
            return {};
        } else {
            return *(reinterpret_cast<T*>(tag + 1));
        }
    }

    const char *getTagString(const char *tagName) const {
        auto tag = gfa_aux_get(static_cast<int>(m_gfaAux.l_aux), m_gfaAux.aux, tagName);
        if (tag == nullptr) {
            return nullptr;
        } else {
            return reinterpret_cast<const char *>(tag + 1);
        }
    }

  private:
    gfa_aux_t &m_gfaAux;
};


class SegmentWrapper {
  public:
    explicit SegmentWrapper(gfa_seg_t &gfaSeg) : m_gfaSeg(gfaSeg) {}

    TagsWrapper getTagsWrapper() const {
        return TagsWrapper(m_gfaSeg.aux);
    }

    const char *getName() const {
        return m_gfaSeg.name;
    }

    const char *getSequence() const {
        return m_gfaSeg.seq;
    }

  private:
    gfa_seg_t &m_gfaSeg;
};


class LinkWrapper {
  public:
    explicit LinkWrapper(gfa_arc_t &gfaArc) : m_gfaArc(gfaArc) {}

    int64_t getStartVertexId() const {
        return gfa_arc_head(m_gfaArc);
    }

    int64_t getEndVertexId() const {
        return gfa_arc_tail(m_gfaArc);
    }

    int64_t fromOverlapLength() const {
        return m_gfaArc.ov;
    }

    int64_t toOverlapLength() const {
        return m_gfaArc.ow;
    }

  private:
    gfa_arc_t &m_gfaArc;
};


class Wrapper{
  private:
    using GfaPtr = std::unique_ptr<gfa_t, void(*)(gfa_t*)>;

  public:
    explicit Wrapper(gfa_t *gfaRawPointer) : m_gfaPtr(gfaRawPointer, gfa_destroy) {}

    int64_t segmentsCount() const {
        return m_gfaPtr->n_seg;
    }

    int64_t verticesCount() const {
        return gfa_n_vtx(m_gfaPtr);
    }

    int64_t linksCount() const {
        return static_cast<int64_t>(m_gfaPtr->n_arc);
    }

    int64_t pathsCount() const {
        return m_gfaPtr->n_path;
    }

    SegmentWrapper getNthSegment(int64_t n) const {
        return SegmentWrapper(m_gfaPtr->seg[n]);
    }

    int64_t countLeavingLinksFromVertex(int64_t vertexId) {
        return gfa_arc_n(m_gfaPtr, vertexId);
    }

    LinkWrapper getNthLeavingLinkFromVertex(int64_t vertexId, int64_t n) {
        return LinkWrapper(gfa_arc_a(m_gfaPtr, vertexId)[n]);
    }

    int64_t getNthLeavingLinkIdFromVertex(int64_t vertexId, int64_t n) {
        return gfa_arc_a(m_gfaPtr, vertexId) + n - m_gfaPtr->arc;
    }

    LinkWrapper getLinkById(int64_t linkId) const {
        return LinkWrapper(m_gfaPtr->arc[linkId]);
    }

    static int64_t getVertexId(int64_t segmentId) {
        return segmentId << 1;
    }

    static int64_t getComplementVertexId(int64_t vertexId) {
        return vertexId ^ 1;
    }

    int64_t countPaths() const {
        return m_gfaPtr->n_path;
    }

    PathWrapper getNthPath(int64_t n) const {
        return PathWrapper(m_gfaPtr->path[n]);
    }

  private:
    GfaPtr m_gfaPtr;
};


Wrapper load(const std::filesystem::path &filePath) {
    return Wrapper(gfa_read(filePath.c_str()));
}

}


#endif //BANDAGE_GRAPH_GFAWRAPPER_HPP_
