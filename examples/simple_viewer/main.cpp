#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <glm/glm.hpp>

#include "FEDeformation.h"
#include "FEModel.h"
#include "FEMeshConverter.h"
#include "FERenderData.h"
#include "FEResultMapper.h"
#include "GLWidget.h"
#include "Theme.h"

class SampleWindow : public QMainWindow {
public:
    SampleWindow() {
        setWindowTitle("FERender Simple Viewer");
        resize(1100, 760);

        viewer_ = new GLWidget(this);
        viewer_->applyTheme(Theme::dark());
        viewer_->setPickMode(PickMode::Element);
        viewer_->setShowLabels(true);

        auto* root = new QWidget(this);
        auto* layout = new QHBoxLayout(root);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(viewer_, 1);
        layout->addWidget(createSidePanel());
        setCentralWidget(root);

        auto* toolbar = addToolBar("Viewer");
        auto* reloadAction = toolbar->addAction("Reload Sample");
        auto* contourAction = toolbar->addAction("Show Sample Contour");
        auto* deformAction = toolbar->addAction("Show Deformed");
        auto* fitAction = toolbar->addAction("Fit");
        auto* themeAction = toolbar->addAction("Toggle Theme");

        connect(reloadAction, &QAction::triggered, this, [this] { loadSampleModel(); });
        connect(contourAction, &QAction::triggered, this, [this] { showSampleContour(); });
        connect(deformAction, &QAction::triggered, this, [this] { showDeformed(); });
        connect(fitAction, &QAction::triggered, this, [this] { fitModel(); });
        connect(themeAction, &QAction::triggered, this, [this] { toggleTheme(); });

        connect(viewer_, &GLWidget::selectionChanged,
                this, [this](PickMode mode, int count, const std::vector<int>& ids) {
            QString modeText = (mode == PickMode::Node) ? "Node" :
                               (mode == PickMode::Element) ? "Element" : "Part";
            selectionLabel_->setText(QString("%1 selected: %2").arg(modeText).arg(count));
            if (!ids.empty()) {
                selectionLabel_->setText(selectionLabel_->text() + QString("  first id: %1").arg(ids.front()));
            }
        });

        loadSampleModel();
    }

private:
    QWidget* createSidePanel() {
        auto* panel = new QWidget(this);
        panel->setFixedWidth(260);
        panel->setStyleSheet(
            "QWidget { background: #181825; color: #cdd6f4; font-size: 13px; }"
            "QLabel#title { font-size: 18px; font-weight: bold; color: #89b4fa; }"
            "QLabel#hint { color: #a6adc8; line-height: 1.4; }"
            "QPushButton { background: #313244; color: #cdd6f4; border: 1px solid #585b70;"
            "  border-radius: 5px; padding: 7px 10px; }"
            "QPushButton:hover { background: #45475a; border-color: #89b4fa; }");

        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(10);

        auto* title = new QLabel("FERender Example", panel);
        title->setObjectName("title");
        layout->addWidget(title);

        auto* hint = new QLabel(
            "This Qt app is built outside the engine and links to the installed FERender package.\n\n"
            "Drag to orbit, scroll to zoom, and click the cube to test element picking.",
            panel);
        hint->setObjectName("hint");
        hint->setWordWrap(true);
        layout->addWidget(hint);

        statsLabel_ = new QLabel(panel);
        statsLabel_->setWordWrap(true);
        layout->addWidget(statsLabel_);

        selectionLabel_ = new QLabel("Element selected: 0", panel);
        selectionLabel_->setWordWrap(true);
        layout->addWidget(selectionLabel_);

        auto* fitBtn = new QPushButton("Fit Model", panel);
        connect(fitBtn, &QPushButton::clicked, this, [this] { fitModel(); });
        layout->addWidget(fitBtn);

        auto* contourBtn = new QPushButton("Show Sample Contour", panel);
        connect(contourBtn, &QPushButton::clicked, this, [this] { showSampleContour(); });
        layout->addWidget(contourBtn);

        auto* deformBtn = new QPushButton("Show Deformed", panel);
        connect(deformBtn, &QPushButton::clicked, this, [this] { showDeformed(); });
        layout->addWidget(deformBtn);

        auto* reloadBtn = new QPushButton("Reload Sample", panel);
        connect(reloadBtn, &QPushButton::clicked, this, [this] { loadSampleModel(); });
        layout->addWidget(reloadBtn);

        layout->addStretch(1);
        return panel;
    }

    static FEModel makeHexModel() {
        FEModel model;
        model.name = "Sample HEX8";

        model.addNode(1, {-1.0f, -1.0f, -1.0f});
        model.addNode(2, { 1.0f, -1.0f, -1.0f});
        model.addNode(3, { 1.0f,  1.0f, -1.0f});
        model.addNode(4, {-1.0f,  1.0f, -1.0f});
        model.addNode(5, {-1.0f, -1.0f,  1.0f});
        model.addNode(6, { 1.0f, -1.0f,  1.0f});
        model.addNode(7, { 1.0f,  1.0f,  1.0f});
        model.addNode(8, {-1.0f,  1.0f,  1.0f});
        model.addElement(1001, ElementType::HEX8, {1, 2, 3, 4, 5, 6, 7, 8});

        FEPart part;
        part.name = "Cube Part";
        part.nodeIds = {1, 2, 3, 4, 5, 6, 7, 8};
        part.elementIds = {1001};
        model.parts.push_back(part);

        return model;
    }

    void loadSampleModel() {
        model_ = makeHexModel();
        renderData_ = FEMeshConverter::toRenderData(model_);

        viewer_->setMesh(renderData_.mesh);
        viewer_->setTriangleToElementMap(renderData_.triangleToElement);
        viewer_->setVertexToNodeMap(renderData_.vertexToNode);
        viewer_->setTriangleToPartMap(renderData_.triangleToPart);
        viewer_->setEdgeToPartMap(renderData_.edgeToPart);
        viewer_->setObjectColor(glm::vec3(0.55f, 0.75f, 0.73f));
        viewer_->setUseVertexColor(false);
        viewer_->setColorBarVisible(false);
        fitModel();

        statsLabel_->setText(QString(
            "Model: %1\nNodes: %2\nElements: %3\nRender vertices: %4\nTriangles: %5")
            .arg(QString::fromStdString(model_.name))
            .arg(model_.nodeCount())
            .arg(model_.elementCount())
            .arg(renderData_.vertexCount())
            .arg(renderData_.triangleCount()));
        selectionLabel_->setText("Element selected: 0");
    }

    void showSampleContour() {
        FEScalarField field;
        field.name = "Sample Temperature";
        field.unit = "degC";
        field.location = FieldLocation::Node;
        field.values = {
            {1, 15.0f},
            {2, 25.0f},
            {3, 35.0f},
            {4, 45.0f},
            {5, 55.0f},
            {6, 65.0f},
            {7, 75.0f},
            {8, 85.0f},
        };

        FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(field, renderData_, model_);
        viewer_->setVertexScalars(mapped.scalars, mapped.minValue, mapped.maxValue, 9);
        viewer_->setColorBarVisible(true);
        viewer_->setColorBarRange(mapped.minValue, mapped.maxValue);
        viewer_->setColorBarTitle("Sample Temperature [degC]");
        viewer_->setColorBarIdLabel("Node ID");
        viewer_->setColorBarExtremes(mapped.minId, mapped.minValue, mapped.maxId, mapped.maxValue);
    }

    void showDeformed() {
        FEVectorField disp;
        disp.name = "Sample Displacement";
        disp.unit = "mm";
        // 顶部四个节点向上拉伸，底部不动
        disp.values = {
            {1, {0.0f, 0.0f, 0.0f}},
            {2, {0.0f, 0.0f, 0.0f}},
            {3, {0.0f, 0.0f, 0.0f}},
            {4, {0.0f, 0.0f, 0.0f}},
            {5, {0.1f, 0.0f, 0.3f}},
            {6, {-0.1f, 0.0f, 0.3f}},
            {7, {-0.1f, 0.0f, 0.3f}},
            {8, {0.1f, 0.0f, 0.3f}},
        };

        FEDeformationOptions opts;
        opts.scale = FEDeformation::autoScale(model_, disp);
        opts.overlayUndeformed = true;

        FEModel deformed = FEDeformation::apply(model_, disp, opts);
        FERenderData defRD = FEMeshConverter::toRenderData(deformed);

        viewer_->setOverlayMesh(renderData_.mesh);
        viewer_->setOverlayVisible(true);

        viewer_->setMesh(defRD.mesh);
        viewer_->setTriangleToElementMap(defRD.triangleToElement);
        viewer_->setVertexToNodeMap(defRD.vertexToNode);
        viewer_->setTriangleToPartMap(defRD.triangleToPart);
        viewer_->setEdgeToPartMap(defRD.edgeToPart);
        viewer_->setObjectColor(glm::vec3(0.55f, 0.75f, 0.73f));
        fitModel();

        statsLabel_->setText(QString(
            "Deformed (scale: %1)\nNodes: %2\nElements: %3")
            .arg(static_cast<double>(opts.scale), 0, 'f', 2)
            .arg(deformed.nodeCount())
            .arg(deformed.elementCount()));
    }

    void fitModel() {
        float size = model_.computeSize();
        if (size > 0.0f) {
            viewer_->fitToModel(model_.computeCenter(), size);
        }
    }

    void toggleTheme() {
        darkTheme_ = !darkTheme_;
        viewer_->applyTheme(darkTheme_ ? Theme::dark() : Theme::light());
    }

    GLWidget* viewer_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    FEModel model_;
    FERenderData renderData_;
    bool darkTheme_ = true;
};

int main(int argc, char* argv[]) {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(32);
    fmt.setSamples(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    SampleWindow window;
    window.show();
    return app.exec();
}
