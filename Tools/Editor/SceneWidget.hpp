#ifndef SCENEWIDGET_HPP
#define SCENEWIDGET_HPP

#include <memory>
#include <list>
#include <vector>
#include <QOpenGLWidget>
#include <QDesktopWidget>
#include <QTimer>
#include "GameObject.hpp"
#include "CameraComponent.hpp"
#include "SpriteRendererComponent.hpp"
#include "Texture2D.hpp"
#include "Scene.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Shader.hpp"
#include "Vec3.hpp"

class SceneWidget : public QOpenGLWidget
{
    Q_OBJECT

public slots:
    void UpdateCamera();

public:
    enum class GizmoAxis { None, X, Y, Z };

    explicit SceneWidget( QWidget* parent = 0 );

    void Init();

    /// \return Index of created game object.
    /// Should only be called by CreateGoCommand!
    ae3d::GameObject* CreateGameObject();

    /// \param index Index.
    /// TODO: Implement as a command.
    void RemoveGameObject( int index );

    /// \return Game object count.
    int GetGameObjectCount() const { return (int)gameObjects.size(); }

    /// \param path Path to .scene file.
    void LoadSceneFromFile( const char* path );

    void RemoveEditorObjects();

    void AddEditorObjects();

    /// \return scene.
    ae3d::Scene* GetScene() { return &scene; }

    ae3d::GameObject* GetGameObject( int index ) { return gameObjects.at( index ).get(); }

    void SetMainWindow( QWidget* aMainWindow ) { mainWindow = aMainWindow; }

    // TODO: Maybe create an editor state class and put this there.
    std::list< int > selectedGameObjects;

signals:
    void GameObjectsAddedOrDeleted();

protected:
    void initializeGL();
    void paintGL();
    void resizeGL( int width, int height );
    void updateGL();
    void keyPressEvent( QKeyEvent* event );
    void keyReleaseEvent( QKeyEvent* event );
    void wheelEvent(QWheelEvent *event);
    void mousePressEvent( QMouseEvent* event );
    void mouseReleaseEvent( QMouseEvent* event );
    bool eventFilter(QObject *obj, QEvent *event);

private slots:
    void GameObjectSelected( std::list< ae3d::GameObject* > gameObjects );

private:
    enum class MouseMode { Grab, Pan, Normal };

    struct TransformGizmo
    {
        void Init( ae3d::Shader* shader );
        void SetPosition( const ae3d::Vec3& position );

        ae3d::GameObject go;
        ae3d::Mesh translateMesh;
        ae3d::Material xAxisMaterial;
        ae3d::Material yAxisMaterial;
        ae3d::Material zAxisMaterial;
        ae3d::Texture2D translateTex;
    };

    TransformGizmo transformGizmo;
    ae3d::GameObject camera;
    ae3d::Texture2D spriteTex;
    ae3d::Scene scene;
    ae3d::Material cubeMaterial;
    ae3d::Mesh cubeMesh;
    ae3d::Shader unlitShader;
    ae3d::Vec3 cameraMoveDir;

    MouseMode mouseMode = MouseMode::Normal;
    GizmoAxis dragAxis = GizmoAxis::None;
    int lastMousePosition[ 2 ];
    QTimer myTimer;
    QDesktopWidget desktop;
    QWidget* mainWindow = nullptr;
    std::vector< std::shared_ptr< ae3d::GameObject > > gameObjects;
};

#endif