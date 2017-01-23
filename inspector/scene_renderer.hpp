#ifndef SCENE_RENDERER_HPP
#define SCENE_RENDERER_HPP

#include <QtCore/QtGlobal>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QOpenGLWidget>

#include <osg/ref_ptr>
#include <osg/Camera>
#include <osg/StateSet>
#include <osg/ShapeDrawable>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>
#include <osgGA/EventQueue>
#include <osgGA/TrackballManipulator>

// Based on an example from https://github.com/vicrucann/QtOSG-hello
class SceneRenderer : public QOpenGLWidget
{
    Q_OBJECT
public:
    SceneRenderer();

public:
    explicit SceneRenderer(QWidget* parent = nullptr)
        : QOpenGLWidget(parent)
        , m_graphics_window(new osgViewer::GraphicsWindowEmbedded(this->x(), this->y(),
                                                                  this->width(), this->height()))
        , m_viewer(new osgViewer::Viewer)
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setDepthBufferSize(24);
        fmt.setSamples(8);
        setFormat(fmt);
        setMouseTracking(true);

        float aspectRatio = static_cast<float>(this->width()) / static_cast<float>(this->height());
        osg::Camera* camera = m_viewer->getCamera();
        camera->setProjectionMatrixAsPerspective(30, aspectRatio, 1, 100);
        camera->setGraphicsContext(m_graphics_window);
        camera->setViewport(0, 0, this->width(), this->height());

        m_viewer->setLightingMode(osgViewer::Viewer::NO_LIGHT);
        m_viewer->setCameraManipulator(new osgGA::TrackballManipulator);
        m_viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
        m_viewer->realize();
    }

    virtual ~SceneRenderer() {}

    void setSceneData(osg::Node* node) {
        m_viewer->setSceneData(node);
        update();
    }

    osg::Vec3f eye() const { return m_eye; }

    osg::Vec3f center() const { return m_center; }

    osg::Vec3f up() const { return m_up; }

private:
    virtual void paintGL() {
        m_viewer->frame();

        osg::Vec3f eye, center, up;
        m_viewer->getCamera()->getViewMatrixAsLookAt(eye, center, up);
        setEye(eye);
        setCenter(center);
        setUp(up);
    }

    virtual void resizeGL(int width, int height) {
        this->getEventQueue()->windowResize(this->x(), this->y(), width, height);
        m_graphics_window->resized(this->x(), this->y(), width, height);
        m_viewer->getCamera()->setViewport(0, 0, this->width(), this->height());
    }

    virtual void initializeGL() {
//        osg::PositionAttitudeTransform* geode = dynamic_cast<osg::PositionAttitudeTransform*>(m_viewer->getSceneData());
//        osg::StateSet* stateSet = geode->getOrCreateStateSet();
//        osg::Material* material = new osg::Material;
//        material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
//        stateSet->setAttributeAndModes(material, osg::StateAttribute::ON);
//        //stateSet->setAttributeAndModes(new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE));
//        stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
    }

    virtual void mouseMoveEvent(QMouseEvent* event) {
        getEventQueue()->mouseMotion(event->x(), event->y());
    }

    virtual void mousePressEvent(QMouseEvent* event) {
        getEventQueue()->mouseButtonPress(event->x(), event->y(), button(event));
    }

    virtual void mouseReleaseEvent(QMouseEvent* event) {
        getEventQueue()->mouseButtonRelease(event->x(), event->y(), button(event));
    }

    virtual void wheelEvent(QWheelEvent* event) {
        int delta = event->delta();
        osgGA::GUIEventAdapter::ScrollingMotion motion = delta > 0 ?
                    osgGA::GUIEventAdapter::SCROLL_UP : osgGA::GUIEventAdapter::SCROLL_DOWN;
        getEventQueue()->mouseScroll(motion);
    }

    virtual bool event(QEvent* event) {
        bool handled = QOpenGLWidget::event(event);
        update();
        return handled;
    }

signals:
    void eyeChanged();
    void centerChanged();
    void upChanged();

private:
    unsigned int button(const QMouseEvent* e) const {
        switch (e->button()) {
        case Qt::LeftButton:
            return 1;
        case Qt::MiddleButton:
            return 2;
        case Qt::RightButton:
            return 3;
        default:
            return 0;
        }
    }

    void setEye(const osg::Vec3f& eye) {
        if (eye != m_eye) {
            m_eye = eye;
            emit eyeChanged();
        }
    }

    void setCenter(const osg::Vec3f center) {
        if (center != m_center) {
            m_center = center;
            emit centerChanged();
        }
    }

    void setUp(const osg::Vec3f& up) {
        if (up != m_up) {
            m_up = up;
            emit upChanged();
        }
    }

    osgGA::EventQueue* getEventQueue() const {
        osgGA::EventQueue* eventQueue = m_graphics_window->getEventQueue();
        return eventQueue;
    }

private:
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> m_graphics_window;
    osg::ref_ptr<osgViewer::Viewer> m_viewer;
    osg::Vec3f m_eye, m_center, m_up;
};

#endif // SCENE_RENDERER_HPP
