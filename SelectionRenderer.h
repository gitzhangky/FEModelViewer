#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

class GLWidget;
class QOpenGLShaderProgram;

class SelectionRenderer {
public:
    explicit SelectionRenderer(GLWidget& w);

    void initGL();
    void resetForMesh();
    void markSelectionDirty();
    void markSilhouetteDirty();
    void markEdgeAdjacencyDirty();
    void invalidatePartEdgeCache();
    void clearHighlight();

    void update();
    void render(QOpenGLShaderProgram& shader);

private:
    struct SilhouetteCandidate {
        float ax, ay, az, bx, by, bz;  // 边两端顶点坐标
        glm::vec3 n0, n1;              // 两侧三角形法线
    };

    struct PreEdge {
        unsigned int va = 0;
        unsigned int vb = 0;
        std::vector<int> adjTris;
    };

    void rebuildSelectionEdges();
    void buildPartEdgeCache();
    void updateSilhouetteFromCache();
    void buildEdgeAdjacency();
    void uploadHighlightVertices(const std::vector<float>& verts);

    GLWidget& w_;

    QOpenGLVertexArrayObject selEdgeVao_;
    QOpenGLBuffer selEdgeVbo_{QOpenGLBuffer::VertexBuffer};
    int selEdgeVertCount_ = 0;
    bool selectionDirty_ = false;
    bool silhouetteDirty_ = false;    // 仅视角变化，需刷新轮廓边
    int selHlMode_ = 0;               // 0=lines, 1=points

    std::vector<float> cachedStaticEdgeVerts_;
    std::vector<SilhouetteCandidate> cachedSilhouettes_;
    bool partEdgeCacheValid_ = false;

    std::unordered_map<int64_t, PreEdge> edgeAdjMap_;
    bool edgeAdjDirty_ = true;
};
