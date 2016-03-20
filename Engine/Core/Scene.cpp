#include "Scene.hpp"
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>
#include <cmath>
#include "AudioSourceComponent.hpp"
#include "AudioSystem.hpp"
#include "CameraComponent.hpp"
#include "DirectionalLightComponent.hpp"
#include "FileSystem.hpp"
#include "Frustum.hpp"
#include "GameObject.hpp"
#include "GfxDevice.hpp"
#include "Matrix.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "MeshRendererComponent.hpp"
#include "RenderTexture.hpp"
#include "SpriteRendererComponent.hpp"
#include "SpotLightComponent.hpp"
#include "TextRendererComponent.hpp"
#include "TransformComponent.hpp"
#include "Texture2D.hpp"
#include "Renderer.hpp"
#include "System.hpp"

using namespace ae3d;
extern ae3d::Renderer renderer;
float GetVRFov();

namespace MathUtil
{
    void GetMinMax( const std::vector< Vec3 >& aPoints, Vec3& outMin, Vec3& outMax );
    
    float Floor( float f )
    {
        return std::floor( f );
    }
    
    bool IsNaN( float f )
    {
        return f != f;
    }

    bool IsPowerOfTwo( unsigned i )
    {
        return ((i & (i - 1)) == 0);
    }
}

namespace Global
{
    extern Vec3 vrEyePosition;
}

namespace SceneGlobal
{
    GameObject shadowCamera;
    bool isShadowCameraCreated = false;
    Matrix44 shadowCameraViewMatrix;
    Matrix44 shadowCameraProjectionMatrix;
}

void SetupCameraForSpotShadowCasting( const Vec3& lightPosition, const Vec3& lightDirection, ae3d::CameraComponent& outCamera,
                                     ae3d::TransformComponent& outCameraTransform )
{
    outCameraTransform.LookAt( lightPosition, lightPosition + lightDirection * 200, Vec3( 0, 1, 0 ) );
    outCamera.SetProjectionType( ae3d::CameraComponent::ProjectionType::Perspective );
    outCamera.SetProjection( 45, 1, 1, 200 );
}

void SetupCameraForDirectionalShadowCasting( const Vec3& lightDirection, const Frustum& eyeFrustum, const Vec3& sceneAABBmin, const Vec3& sceneAABBmax,
                                             ae3d::CameraComponent& outCamera, ae3d::TransformComponent& outCameraTransform )
{
    const Vec3 viewFrustumCentroid = eyeFrustum.Centroid();

    System::Assert( !MathUtil::IsNaN( lightDirection.x ) && !MathUtil::IsNaN( lightDirection.y ) && !MathUtil::IsNaN( lightDirection.z ), "Invalid light direction" );
    System::Assert( !MathUtil::IsNaN( sceneAABBmin.x ) && !MathUtil::IsNaN( sceneAABBmin.y ) && !MathUtil::IsNaN( sceneAABBmin.z ), "Invalid scene AABB min" );
    System::Assert( !MathUtil::IsNaN( sceneAABBmax.x ) && !MathUtil::IsNaN( sceneAABBmax.y ) && !MathUtil::IsNaN( sceneAABBmax.z ), "Invalid scene AABB max" );
    System::Assert( !MathUtil::IsNaN( viewFrustumCentroid.x ) && !MathUtil::IsNaN( viewFrustumCentroid.y ) && !MathUtil::IsNaN( viewFrustumCentroid.z ), "Invalid eye frustum" );
    System::Assert( outCamera.GetTargetTexture() != nullptr, "Shadow camera needs target texture" );
    System::Assert( lightDirection.Length() > 0.9f && lightDirection.Length() < 1.1f, "Light dir must be normalized" );
    

    // Start at the centroid, and move back in the opposite direction of the light
    // by an amount equal to the camera's farClip. This is the temporary working position for the light.
    // TODO: Verify math. + was - in my last engine but had to be changed to get the right direction [TimoW, 2015-09-26]
    const Vec3 shadowCameraPosition = viewFrustumCentroid + lightDirection * eyeFrustum.FarClipPlane();
    
    outCameraTransform.LookAt( shadowCameraPosition, viewFrustumCentroid, Vec3( 0, 1, 0 ) );
    
    Matrix44 view;
    outCameraTransform.GetLocalRotation().GetMatrix( view );
    Matrix44 translation;
    translation.Translate( -outCameraTransform.GetLocalPosition() );
    Matrix44::Multiply( translation, view, view );
    outCamera.SetView( view );

    const Matrix44& shadowCameraView = outCamera.GetView();
    
    // Transforms view camera frustum points to shadow camera space.
    std::vector< Vec3 > viewFrustumLS( 8 );
    
    Matrix44::TransformPoint( eyeFrustum.NearTopLeft(), shadowCameraView, &viewFrustumLS[ 0 ] );
    Matrix44::TransformPoint( eyeFrustum.NearTopRight(), shadowCameraView, &viewFrustumLS[ 1 ] );
    Matrix44::TransformPoint( eyeFrustum.NearBottomLeft(), shadowCameraView, &viewFrustumLS[ 2 ] );
    Matrix44::TransformPoint( eyeFrustum.NearBottomRight(), shadowCameraView, &viewFrustumLS[ 3 ] );
    
    Matrix44::TransformPoint( eyeFrustum.FarTopLeft(), shadowCameraView, &viewFrustumLS[ 4 ] );
    Matrix44::TransformPoint( eyeFrustum.FarTopRight(), shadowCameraView, &viewFrustumLS[ 5 ] );
    Matrix44::TransformPoint( eyeFrustum.FarBottomLeft(), shadowCameraView, &viewFrustumLS[ 6 ] );
    Matrix44::TransformPoint( eyeFrustum.FarBottomRight(), shadowCameraView, &viewFrustumLS[ 7 ] );
    
    // Gets light-space view frustum extremes.
    Vec3 viewMinLS, viewMaxLS;
    MathUtil::GetMinMax( viewFrustumLS, viewMinLS, viewMaxLS );
    
    // Transforms scene's AABB to light-space.
    Vec3 sceneAABBminLS, sceneAABBmaxLS;
    const Vec3 min = sceneAABBmin;
    const Vec3 max = sceneAABBmax;
    
    const Vec3 sceneCorners[] =
    {
        Vec3( min.x, min.y, min.z ),
        Vec3( max.x, min.y, min.z ),
        Vec3( min.x, max.y, min.z ),
        Vec3( min.x, min.y, max.z ),
        Vec3( max.x, max.y, min.z ),
        Vec3( min.x, max.y, max.z ),
        Vec3( max.x, max.y, max.z ),
        Vec3( max.x, min.y, max.z )
    };
    
    std::vector< Vec3 > sceneAABBLS( 8 );
    
    for (int i = 0; i < 8; ++i)
    {
        Matrix44::TransformPoint( sceneCorners[ i ], shadowCameraView, &sceneAABBLS[ i ] );
    }
    
    MathUtil::GetMinMax( sceneAABBLS, sceneAABBminLS, sceneAABBmaxLS );
    
    // Use world volume for near plane.
    viewMaxLS.z = sceneAABBmaxLS.z > viewMaxLS.z ? sceneAABBmaxLS.z : viewMaxLS.z;
   
    outCamera.SetProjectionType( ae3d::CameraComponent::ProjectionType::Orthographic );
    outCamera.SetProjection( viewMinLS.x, viewMaxLS.x, viewMinLS.y, viewMaxLS.y, -viewMaxLS.z, -viewMinLS.z );
}

void ae3d::Scene::Add( GameObject* gameObject )
{
    for (const auto& go : gameObjects)
    {
        if (go == gameObject)
        {
            return;
        }
    }

    if (nextFreeGameObject == gameObjects.size())
    {
        gameObjects.resize( gameObjects.size() + 10 );
    }

    gameObjects[ nextFreeGameObject++ ] = gameObject;
}

void ae3d::Scene::Remove( GameObject* gameObject )
{
    for (std::size_t i = 0; i < gameObjects.size(); ++i)
    {
        if (gameObject == gameObjects[ i ])
        {
            gameObjects.erase( std::begin( gameObjects ) + i );
            return;
        }
    }
}

void ae3d::Scene::Render()
{
#if RENDERER_VULKAN
    GfxDevice::BeginFrame();
#endif
    GenerateAABB();
    GfxDevice::ResetFrameStatistics();
    TransformComponent::UpdateLocalMatrices();

    std::vector< GameObject* > rtCameras;
    rtCameras.reserve( gameObjects.size() / 4 );
    std::vector< GameObject* > cameras;
    cameras.reserve( gameObjects.size() / 4 );
    
    for (auto gameObject : gameObjects)
    {
        if (gameObject == nullptr)
        {
            continue;
        }

        auto cameraComponent = gameObject->GetComponent<CameraComponent>();
        
        if (cameraComponent && cameraComponent->GetTargetTexture() != nullptr && gameObject->GetComponent<TransformComponent>())
        {
            rtCameras.push_back( gameObject );
        }
        if (cameraComponent && cameraComponent->GetTargetTexture() == nullptr && gameObject->GetComponent<TransformComponent>())
        {
            cameras.push_back( gameObject );
        }
    }
    
    auto cameraSortFunction = [](GameObject* g1, GameObject* g2) { return g1->GetComponent< CameraComponent >()->GetRenderOrder() <
        g2->GetComponent< CameraComponent >()->GetRenderOrder(); };
    std::sort( std::begin( cameras ), std::end( cameras ), cameraSortFunction );
    
    for (auto rtCamera : rtCameras)
    {
        auto transform = rtCamera->GetComponent< TransformComponent >();

        if (transform && !rtCamera->GetComponent< CameraComponent >()->GetTargetTexture()->IsCube())
        {
            RenderWithCamera( rtCamera, 0 );
        }
        else if (transform && rtCamera->GetComponent< CameraComponent >()->GetTargetTexture()->IsCube())
        {
            for (int cubeMapFace = 0; cubeMapFace < 6; ++cubeMapFace)
            {
                const float scale = 2000;

                static const Vec3 directions[ 6 ] =
                {
                    Vec3(  1,  0,  0 ) * scale, // posX
                    Vec3( -1,  0,  0 ) * scale, // negX
                    Vec3(  0,  1,  0 ) * scale, // posY
                    Vec3(  0, -1,  0 ) * scale, // negY
                    Vec3(  0,  0,  1 ) * scale, // posZ
                    Vec3(  0,  0, -1 ) * scale  // negZ
                };
                
                static const Vec3 ups[ 6 ] =
                {
                    Vec3( 0, -1,  0 ),
                    Vec3( 0, -1,  0 ),
                    Vec3( 0,  0,  1 ),
                    Vec3( 0,  0, -1 ),
                    Vec3( 0, -1,  0 ),
                    Vec3( 0, -1,  0 )
                };
                
                transform->LookAt( transform->GetLocalPosition(), transform->GetLocalPosition() + directions[ cubeMapFace ], ups[ cubeMapFace ] );
                RenderWithCamera( rtCamera, cubeMapFace );
            }
        }
    }

    //unsigned debugShadowFBO = 0;

    for (auto camera : cameras)
    {
        if (camera != nullptr && camera->GetComponent<TransformComponent>())
        {
            TransformComponent* cameraTransform = camera->GetComponent<TransformComponent>();

            auto cameraPos = cameraTransform->GetLocalPosition();
            auto cameraDir = cameraTransform->GetViewDirection();
            
            if (camera->GetComponent<CameraComponent>()->GetProjectionType() == ae3d::CameraComponent::ProjectionType::Perspective)
            {
                AudioSystem::SetListenerPosition( cameraPos.x, cameraPos.y, cameraPos.z );
                AudioSystem::SetListenerOrientation( cameraDir.x, cameraDir.y, cameraDir.z );
            }

            // Shadow pass
            for (auto go : gameObjects)
            {
                if (!go)
                {
                    continue;
                }
                
                auto lightTransform = go->GetComponent<TransformComponent>();
                auto dirLight = go->GetComponent<DirectionalLightComponent>();
                auto spotLight = go->GetComponent<SpotLightComponent>();

                if (lightTransform && ((dirLight && dirLight->CastsShadow()) || (spotLight && spotLight->CastsShadow())))
                {
                    Frustum eyeFrustum;
                    
                    auto cameraComponent = camera->GetComponent< CameraComponent >();
                    
                    if (cameraComponent->GetProjectionType() == CameraComponent::ProjectionType::Perspective)
                    {
                        eyeFrustum.SetProjection( cameraComponent->GetFovDegrees(), cameraComponent->GetAspect(), cameraComponent->GetNear(), cameraComponent->GetFar() );
                    }
                    else
                    {
                        eyeFrustum.SetProjection( cameraComponent->GetLeft(), cameraComponent->GetRight(), cameraComponent->GetBottom(), cameraComponent->GetTop(), cameraComponent->GetNear(), cameraComponent->GetFar() );
                    }
                    
                    Matrix44 eyeView;
                    cameraTransform->GetLocalRotation().GetMatrix( eyeView );
                    Matrix44 translation;
                    translation.Translate( -cameraTransform->GetLocalPosition() );
                    Matrix44::Multiply( translation, eyeView, eyeView );
                    
                    const Vec3 eyeViewDir = Vec3( eyeView.m[2], eyeView.m[6], eyeView.m[10] ).Normalized();
                    eyeFrustum.Update( cameraTransform->GetLocalPosition(), eyeViewDir );
                    
                    if (!SceneGlobal::isShadowCameraCreated)
                    {
                        SceneGlobal::shadowCamera.AddComponent< CameraComponent >();
                        SceneGlobal::shadowCamera.AddComponent< TransformComponent >();
                        SceneGlobal::isShadowCameraCreated = true;
                    }
                    
                    if (dirLight)
                    {
                        SceneGlobal::shadowCamera.GetComponent< CameraComponent >()->SetTargetTexture( &go->GetComponent<DirectionalLightComponent>()->shadowMap );
                        SetupCameraForDirectionalShadowCasting( lightTransform->GetViewDirection(), eyeFrustum, aabbMin, aabbMax, *SceneGlobal::shadowCamera.GetComponent< CameraComponent >(), *SceneGlobal::shadowCamera.GetComponent< TransformComponent >() );
                    }
                    else if (spotLight)
                    {
                        SceneGlobal::shadowCamera.GetComponent< CameraComponent >()->SetTargetTexture( &go->GetComponent<SpotLightComponent>()->shadowMap );
                        SetupCameraForSpotShadowCasting( lightTransform->GetLocalPosition(), lightTransform->GetViewDirection(), *SceneGlobal::shadowCamera.GetComponent< CameraComponent >(), *SceneGlobal::shadowCamera.GetComponent< TransformComponent >() );
                    }
                    
                    RenderShadowsWithCamera( &SceneGlobal::shadowCamera, 0 );
                    
                    if (dirLight)
                    {
                        Material::SetGlobalRenderTexture( "_ShadowMap", &go->GetComponent<DirectionalLightComponent>()->shadowMap );
                        //debugShadowFBO = go->GetComponent<DirectionalLightComponent>()->shadowMap.GetFBO();
                    }
                    else if (spotLight)
                    {
                        Material::SetGlobalRenderTexture( "_ShadowMap", &go->GetComponent<SpotLightComponent>()->shadowMap );
                        //debugShadowFBO = go->GetComponent<SpotLightComponent>()->shadowMap.GetFBO();
                    }
                }
            }
        }
        
        RenderWithCamera( camera, 0 );
    }
    
    //GfxDevice::DebugBlitFBO( debugShadowFBO, 256, 256 );
}

void ae3d::Scene::RenderWithCamera( GameObject* cameraGo, int cubeMapFace )
{
    ae3d::System::Assert( 0 <= cubeMapFace && cubeMapFace < 6, "invalid cube map face" );

    CameraComponent* camera = cameraGo->GetComponent< CameraComponent >();
    const Vec3 color = camera->GetClearColor();
    GfxDevice::SetClearColor( color.x, color.y, color.z );
    GfxDevice::SetRenderTarget( camera->GetTargetTexture(), cubeMapFace );
#if RENDERER_METAL
    GfxDevice::BeginFrame();
#endif
#if RENDERER_VULKAN
    GfxDevice::BeginRenderPassAndCommandBuffer();
#endif
    if (camera->GetClearFlag() == CameraComponent::ClearFlag::DepthAndColor)
    {
        GfxDevice::ClearScreen( GfxDevice::ClearFlags::Color | GfxDevice::ClearFlags::Depth );
    }
    else if (camera->GetClearFlag() == CameraComponent::ClearFlag::Depth)
    {
        GfxDevice::ClearScreen( GfxDevice::ClearFlags::Depth );
    }
    else if (camera->GetClearFlag() == CameraComponent::ClearFlag::DontClear)
    {
        GfxDevice::ClearScreen( GfxDevice::ClearFlags::DontClear );
    }
    else
    {
        System::Assert( false, "Unhandled clear flag." );
    }

    Matrix44 view;

    if (skybox != nullptr)
    {
        auto cameraTrans = cameraGo->GetComponent< TransformComponent >();
        cameraTrans->GetLocalRotation().GetMatrix( view );
#if OCULUS_RIFT
        Matrix44 vrView = cameraTrans->GetVrView();
        // Cancels translation.
        vrView.m[ 14 ] = 0;
        vrView.m[ 13 ] = 0;
        vrView.m[ 12 ] = 0;

        camera->SetView( vrView );
#else
        camera->SetView( view );
#endif
        renderer.RenderSkybox( skybox, *camera );
    }
    
    float fovDegrees;
    Vec3 position;

    // TODO: Maybe add a VR flag into camera to select between HMD and normal pose.
#if OCULUS_RIFT
    view = cameraGo->GetComponent< TransformComponent >()->GetVrView();
    position = Global::vrEyePosition;
    fovDegrees = GetVRFov();
#else
    auto cameraTransform = cameraGo->GetComponent< TransformComponent >();
    position = cameraTransform->GetLocalPosition();
    fovDegrees = camera->GetFovDegrees();
    cameraTransform->GetLocalRotation().GetMatrix( view );
    Matrix44 translation;
    translation.Translate( -cameraTransform->GetLocalPosition() );
    Matrix44::Multiply( translation, view, view );
#endif
    
    Frustum frustum;
    
    if (camera->GetProjectionType() == CameraComponent::ProjectionType::Perspective)
    {
        frustum.SetProjection( fovDegrees, camera->GetAspect(), camera->GetNear(), camera->GetFar() );
    }
    else
    {
        frustum.SetProjection( camera->GetLeft(), camera->GetRight(), camera->GetBottom(), camera->GetTop(), camera->GetNear(), camera->GetFar() );
    }
    
    const Vec3 viewDir = Vec3( view.m[2], view.m[6], view.m[10] ).Normalized();
    frustum.Update( position, viewDir );

    std::vector< unsigned > gameObjectsWithMeshRenderer;
    gameObjectsWithMeshRenderer.reserve( gameObjects.size() );
    int i = -1;
    
    for (auto gameObject : gameObjects)
    {
        ++i;
        
        if (gameObject == nullptr || (gameObject->GetLayer() & camera->GetLayerMask()) == 0)
        {
            continue;
        }
        
        auto transform = gameObject->GetComponent< TransformComponent >();
        auto spriteRenderer = gameObject->GetComponent< SpriteRendererComponent >();

        if (spriteRenderer)
        {
            Matrix44 projectionModel;
            Matrix44::Multiply( transform ? transform->GetLocalMatrix() : Matrix44::identity, camera->GetProjection(), projectionModel );
            spriteRenderer->Render( projectionModel.m );
        }
        
        auto textRenderer = gameObject->GetComponent< TextRendererComponent >();
        
        if (textRenderer)
        {
            Matrix44 projectionModel;
            Matrix44::Multiply( transform ? transform->GetLocalMatrix() : Matrix44::identity, camera->GetProjection(), projectionModel );
            textRenderer->Render( projectionModel.m );
        }
        
        auto meshRenderer = gameObject->GetComponent< MeshRendererComponent >();

        if (meshRenderer)
        {
            gameObjectsWithMeshRenderer.push_back( i );
        }
    }

    auto meshSorterByMesh = [&](unsigned j, unsigned k) { return gameObjects[ j ]->GetComponent< MeshRendererComponent >()->GetMesh() ==
                                                                 gameObjects[ k ]->GetComponent< MeshRendererComponent >()->GetMesh(); };
    std::sort( std::begin( gameObjectsWithMeshRenderer ), std::end( gameObjectsWithMeshRenderer ), meshSorterByMesh );
    
    for (auto j : gameObjectsWithMeshRenderer)
    {
        auto transform = gameObjects[ j ]->GetComponent< TransformComponent >();
        auto meshLocalToWorld = transform ? transform->GetLocalMatrix() : Matrix44::identity;
        
        Matrix44 mv;
        Matrix44 mvp;
        Matrix44::Multiply( meshLocalToWorld, view, mv );
        Matrix44::Multiply( mv, camera->GetProjection(), mvp );
        
        gameObjects[ j ]->GetComponent< MeshRendererComponent >()->Render( mv, mvp, frustum, meshLocalToWorld, nullptr );
    }
#if RENDERER_METAL
    GfxDevice::PresentDrawable();
#endif
#if RENDERER_VULKAN
    GfxDevice::EndRenderPassAndCommandBuffer();
#endif
    GfxDevice::ErrorCheck( "Scene render after rendering" );

    // Depth and normals
    if (camera->GetDepthNormalsTexture().GetID() == 0)
    {
        return;
    }
    
#if RENDERER_METAL
    GfxDevice::ClearScreen( GfxDevice::ClearFlags::Color | GfxDevice::ClearFlags::Depth );
    GfxDevice::SetRenderTarget( &camera->GetDepthNormalsTexture(), cubeMapFace );
    GfxDevice::BeginFrame();
#else
    GfxDevice::SetRenderTarget( &camera->GetDepthNormalsTexture(), cubeMapFace );
    GfxDevice::ClearScreen( GfxDevice::ClearFlags::Color | GfxDevice::ClearFlags::Depth );
#endif

    for (auto j : gameObjectsWithMeshRenderer)
    {
        auto transform = gameObjects[ j ]->GetComponent< TransformComponent >();
        auto meshLocalToWorld = transform ? transform->GetLocalMatrix() : Matrix44::identity;
        
        Matrix44 mv;
        Matrix44 mvp;
        Matrix44::Multiply( meshLocalToWorld, view, mv );
        Matrix44::Multiply( mv, camera->GetProjection(), mvp );
        
        gameObjects[ j ]->GetComponent< MeshRendererComponent >()->Render( mv, mvp, frustum, meshLocalToWorld, &renderer.builtinShaders.depthNormalsShader );
    }
#if RENDERER_METAL
    GfxDevice::PresentDrawable();
#endif
    GfxDevice::SetRenderTarget( nullptr, 0 );

    GfxDevice::ErrorCheck( "Scene render end" );
}

void ae3d::Scene::RenderShadowsWithCamera( GameObject* cameraGo, int cubeMapFace )
{
    CameraComponent* camera = cameraGo->GetComponent< CameraComponent >();
    GfxDevice::SetRenderTarget( camera->GetTargetTexture(), cubeMapFace );
    
    const Vec3 color = camera->GetClearColor();
    GfxDevice::SetClearColor( color.x, color.y, color.z );
    
    if (camera->GetClearFlag() == CameraComponent::ClearFlag::DepthAndColor)
    {
        GfxDevice::ClearScreen( GfxDevice::ClearFlags::Color | GfxDevice::ClearFlags::Depth );
    }
#if RENDERER_METAL
    GfxDevice::BeginFrame();
#endif

    Matrix44 view;
    auto cameraTransform = cameraGo->GetComponent< TransformComponent >();
    cameraTransform->GetLocalRotation().GetMatrix( view );
    Matrix44 translation;
    translation.Translate( -cameraTransform->GetLocalPosition() );
    Matrix44::Multiply( translation, view, view );
    
    SceneGlobal::shadowCameraViewMatrix = view;
    SceneGlobal::shadowCameraProjectionMatrix = camera->GetProjection();
    
    Frustum frustum;
    
    if (camera->GetProjectionType() == CameraComponent::ProjectionType::Perspective)
    {
        frustum.SetProjection( camera->GetFovDegrees(), camera->GetAspect(), camera->GetNear(), camera->GetFar() );
    }
    else
    {
        frustum.SetProjection( camera->GetLeft(), camera->GetRight(), camera->GetBottom(), camera->GetTop(), camera->GetNear(), camera->GetFar() );
    }
    
    const Vec3 viewDir = Vec3( view.m[2], view.m[6], view.m[10] ).Normalized();
    frustum.Update( cameraTransform->GetLocalPosition(), viewDir );
    
    std::vector< unsigned > gameObjectsWithMeshRenderer;
    gameObjectsWithMeshRenderer.reserve( gameObjects.size() );
    int i = -1;
    
    for (auto gameObject : gameObjects)
    {
        ++i;
        
        if (gameObject == nullptr)
        {
            continue;
        }
        
        auto meshRenderer = gameObject->GetComponent< MeshRendererComponent >();
        
        if (meshRenderer)
        {
            gameObjectsWithMeshRenderer.push_back( i );
        }
    }
    
    auto meshSorterByMesh = [&](unsigned j, unsigned k) { return gameObjects[ j ]->GetComponent< MeshRendererComponent >()->GetMesh() ==
        gameObjects[ k ]->GetComponent< MeshRendererComponent >()->GetMesh(); };
    std::sort( std::begin( gameObjectsWithMeshRenderer ), std::end( gameObjectsWithMeshRenderer ), meshSorterByMesh );
    
    for (auto j : gameObjectsWithMeshRenderer)
    {
        auto transform = gameObjects[ j ]->GetComponent< TransformComponent >();
        auto meshLocalToWorld = transform ? transform->GetLocalMatrix() : Matrix44::identity;
        
        Matrix44 mv;
        Matrix44 mvp;
        Matrix44::Multiply( meshLocalToWorld, view, mv );
        Matrix44::Multiply( mv, camera->GetProjection(), mvp );

        gameObjects[ j ]->GetComponent< MeshRendererComponent >()->Render( mv, mvp, frustum, meshLocalToWorld, &renderer.builtinShaders.momentsShader );
    }
    
#if RENDERER_METAL
    GfxDevice::PresentDrawable();
#endif

    GfxDevice::ErrorCheck( "Scene render shadows end" );
}

void ae3d::Scene::SetSkybox( const TextureCube* skyTexture )
{
    skybox = skyTexture;

    if (skybox != nullptr && !renderer.IsSkyboxGenerated())
    {
        renderer.GenerateSkybox();
    }
}

std::string ae3d::Scene::GetSerialized() const
{
    std::string outSerialized;

    for (auto gameObject : gameObjects)
    {
        if (gameObject == nullptr)
        {
            continue;
        }

        outSerialized += gameObject->GetSerialized();
        
        // TODO: Try to DRY.
        if (gameObject->GetComponent<MeshRendererComponent>())
        {
            outSerialized += gameObject->GetComponent<MeshRendererComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<TransformComponent>())
        {
            outSerialized += gameObject->GetComponent<TransformComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<CameraComponent>())
        {
            outSerialized += gameObject->GetComponent<CameraComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<SpriteRendererComponent>())
        {
            outSerialized += gameObject->GetComponent<SpriteRendererComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<TextRendererComponent>())
        {
            outSerialized += gameObject->GetComponent<TextRendererComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<AudioSourceComponent>())
        {
            outSerialized += gameObject->GetComponent<AudioSourceComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<DirectionalLightComponent>())
        {
            outSerialized += gameObject->GetComponent<DirectionalLightComponent>()->GetSerialized();
        }
        if (gameObject->GetComponent<SpotLightComponent>())
        {
            outSerialized += gameObject->GetComponent<SpotLightComponent>()->GetSerialized();
        }
    }
    return outSerialized;
}

ae3d::Scene::DeserializeResult ae3d::Scene::Deserialize( const FileSystem::FileContentsData& serialized, std::vector< GameObject >& outGameObjects,
                                                        std::map< std::string, Texture2D* >& outTexture2Ds,
                                                        std::map< std::string, Material* >& outMaterials,
                                                        std::vector< Mesh* >& outMeshes ) const
{
    // TODO: It would be better to store the token strings into somewhere accessible to GetSerialized() to prevent typos etc.

    outGameObjects.clear();

    std::stringstream stream( std::string( std::begin( serialized.data ), std::end( serialized.data ) ) );
    std::string line;
    
    std::string currentMaterialName;
    
    while (!stream.eof())
    {
        std::getline( stream, line );
        std::stringstream lineStream( line );
        std::string token;
        lineStream >> token;
        
        if (token == "gameobject")
        {
            outGameObjects.push_back( GameObject() );
            std::string name;
            lineStream >> name;
            outGameObjects.back().SetName( name.c_str() );
        }

        if (token == "dirlight")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found dirlight but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            outGameObjects.back().AddComponent< DirectionalLightComponent >();
        }

        if (token == "shadow")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found shadow but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            int enabled;
            lineStream >> enabled;
            outGameObjects.back().GetComponent< DirectionalLightComponent >()->SetCastShadow( enabled ? true : false, 512 );
        }

        if (token == "camera")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found camera but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            outGameObjects.back().AddComponent< CameraComponent >();
        }

        if (token == "ortho")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found ortho but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            float x, y, width, height, nearp, farp;
            lineStream >> x >> y >> width >> height >> nearp >> farp;
            outGameObjects.back().GetComponent< CameraComponent >()->SetProjection( x, y, width, height, nearp, farp );
        }

        if (token == "persp")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found persp but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            float fov, aspect, nearp, farp;
            lineStream >> fov >> aspect >> nearp >> farp;
            outGameObjects.back().GetComponent< CameraComponent >()->SetProjection( fov, aspect, nearp, farp );
        }

        if (token == "projection")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found projection but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            std::string type;
            lineStream >> type;
            
            if (type == "orthographic")
            {
                outGameObjects.back().GetComponent< CameraComponent >()->SetProjectionType( ae3d::CameraComponent::ProjectionType::Orthographic );
            }
            else if (type == "perspective")
            {
                outGameObjects.back().GetComponent< CameraComponent >()->SetProjectionType( ae3d::CameraComponent::ProjectionType::Perspective );
            }
            else
            {
                System::Print( "Camera has unknown projection type %s\n", type.c_str() );
                return DeserializeResult::ParseError;
            }
        }

        if (token == "clearcolor")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found clearcolor but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            float red, green, blue;
            lineStream >> red >> green >> blue;
            outGameObjects.back().GetComponent< CameraComponent >()->SetClearColor( { red, green, blue } );
        }

        if (token == "transform")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found transform but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            outGameObjects.back().AddComponent< TransformComponent >();
        }

        if (token == "meshrenderer")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found meshrenderer but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            std::string meshFile;
            lineStream >> meshFile;
            
            outGameObjects.back().AddComponent< MeshRendererComponent >();
            
            outMeshes.push_back( new Mesh() );
            outMeshes.back()->Load( FileSystem::FileContents( meshFile.c_str() ) );
            outGameObjects.back().GetComponent< MeshRendererComponent >()->SetMesh( outMeshes.back() );
        }

        if (token == "position")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found position but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            if (!outGameObjects.back().GetComponent< TransformComponent >())
            {
                System::Print( "Failed to parse %s: found position but the game object doesn't have a transform component.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            float x, y, z;
            lineStream >> x >> y >> z;
            outGameObjects.back().GetComponent< TransformComponent >()->SetLocalPosition( { x, y, z } );
        }

        if (token == "rotation")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found rotation but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            if (!outGameObjects.back().GetComponent< TransformComponent >())
            {
                System::Print( "Failed to parse %s: found rotation but the game object doesn't have a transform component.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            float x, y, z, s;
            lineStream >> x >> y >> z >> s;
            outGameObjects.back().GetComponent< TransformComponent >()->SetLocalRotation( { { x, y, z }, s } );
        }

        if (token == "scale")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found scale but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }
            
            if (!outGameObjects.back().GetComponent< TransformComponent >())
            {
                System::Print( "Failed to parse %s: found scale but the game object doesn't have a transform component.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }

            float scale;
            lineStream >> scale;
            outGameObjects.back().GetComponent< TransformComponent >()->SetLocalScale( scale );
        }

        if (token == "texture2d")
        {
            std::string name;
            std::string path;
            
            lineStream >> name >> path;
            
            outTexture2Ds[ name ] = new Texture2D();
            outTexture2Ds[ name ]->Load( FileSystem::FileContents( path.c_str() ), TextureWrap::Repeat, TextureFilter::Linear, Mipmaps::Generate, ColorSpace::SRGB, 1 );
        }

        if (token == "material")
        {
            std::string materialName;
            lineStream >> materialName;
            currentMaterialName = materialName;
            
            outMaterials[ materialName ] = new Material();
        }

        if (token == "mesh_material")
        {
            if (outGameObjects.empty())
            {
                System::Print( "Failed to parse %s: found mesh_material but there are no game objects defined before this line.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }
            
            auto mr = outGameObjects.back().GetComponent< MeshRendererComponent >();

            if (!mr)
            {
                System::Print( "Failed to parse %s: found mesh_material but the last defined game object doesn't have a mesh renderer component.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }
            
            if (!mr->GetMesh())
            {
                System::Print( "Failed to parse %s: found mesh_material but the last defined game object's mesh renderer doesn't have a mesh.\n", serialized.path.c_str() );
                return DeserializeResult::ParseError;
            }
            
            std::string meshName;
            std::string materialName;
            lineStream >> meshName >> materialName;
            
            const unsigned subMeshCount = mr->GetMesh()->GetSubMeshCount();
            
            for (unsigned i = 0; i < subMeshCount; ++i)
            {
                if (mr->GetMesh()->GetSubMeshName( i ) == meshName)
                {
                    mr->SetMaterial( outMaterials[ materialName ], i );
                }
            }
        }

        if (token == "param_texture")
        {
            std::string uniformName;
            std::string textureName;
            
            lineStream >> uniformName >> textureName;
            outMaterials[ currentMaterialName ]->SetTexture( uniformName.c_str(), outTexture2Ds[ textureName ]);
        }
    }

    return DeserializeResult::Success;
}

void ae3d::Scene::GenerateAABB()
{
    const float maxValue = 99999999.0f;
    aabbMin = {  maxValue,  maxValue,  maxValue };
    aabbMax = { -maxValue, -maxValue, -maxValue };
    
    for (auto& o : gameObjects)
    {
        if (!o)
        {
            continue;
        }

        auto meshRenderer = o->GetComponent< ae3d::MeshRendererComponent >();
        auto meshTransform = o->GetComponent< ae3d::TransformComponent >();

        if (meshRenderer == nullptr || meshTransform == nullptr)
        {
            continue;
        }
        
        Vec3 oAABBmin = meshRenderer->GetMesh() ? meshRenderer->GetMesh()->GetAABBMin() : Vec3( -1, -1, -1 );
        Vec3 oAABBmax = meshRenderer->GetMesh() ? meshRenderer->GetMesh()->GetAABBMax() : Vec3(  1,  1,  1 );
        
        Matrix44::TransformPoint( oAABBmin, meshTransform->GetLocalMatrix(), &oAABBmin );
        Matrix44::TransformPoint( oAABBmax, meshTransform->GetLocalMatrix(), &oAABBmax );
        
        if (oAABBmin.x < aabbMin.x)
        {
            aabbMin.x = oAABBmin.x;
        }
        
        if (oAABBmin.y < aabbMin.y)
        {
            aabbMin.y = oAABBmin.y;
        }
        
        if (oAABBmin.z < aabbMin.z)
        {
            aabbMin.z = oAABBmin.z;
        }
        
        if (oAABBmax.x > aabbMax.x)
        {
            aabbMax.x = oAABBmax.x;
        }
        
        if (oAABBmax.y > aabbMax.y)
        {
            aabbMax.y = oAABBmax.y;
        }
        
        if (oAABBmax.z > aabbMax.z)
        {
            aabbMax.z = oAABBmax.z;
        }
    }
}
