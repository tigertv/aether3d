#include <string>
#include <stdint.h>
#include "Array.hpp"
#include "AudioClip.hpp"
#include "AudioSourceComponent.hpp"
#include "CameraComponent.hpp"
#include "ComputeShader.hpp"
#include "DirectionalLightComponent.hpp"
#include "FileSystem.hpp"
#include "Font.hpp"
#include "GameObject.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "MeshRendererComponent.hpp"
#include "PointLightComponent.hpp"
#include "RenderTexture.hpp"
#include "SpriteRendererComponent.hpp"
#include "SpotLightComponent.hpp"
#include "Scene.hpp"
#include "Shader.hpp"
#include "System.hpp"
#include "TextRendererComponent.hpp"
#include "TransformComponent.hpp"
#include "Texture2D.hpp"
#include "TextureCube.hpp"
#include "Vec3.hpp"
#include "VR.hpp"
#include "Window.hpp"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#if RENDERER_METAL
#include "nuklear.h"
#else
#include "../NuklearTest/nuklear.h"
#endif

// Assets for this sample (extract into aether3d_build/Samples): http://twiren.kapsi.fi/files/aether3d_sample_v0.8.1.zip

using namespace ae3d;

struct VertexPTC
{
    float position[ 3 ];
    float uv[ 2 ];
    float col[ 4 ];
};

nk_draw_null_texture nullTexture;
Texture2D* uiTextures[ 1 ];

void DrawNuklear( nk_context* ctx, nk_buffer* uiCommands, int width, int height )
{
    nk_convert_config config = {};
    static const nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF( VertexPTC, position )},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF( VertexPTC, uv )},
        {NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT, NK_OFFSETOF( VertexPTC, col )},
        {NK_VERTEX_LAYOUT_END}
    };

    config.vertex_layout = vertex_layout;
    config.vertex_size = sizeof( VertexPTC );
    config.vertex_alignment = NK_ALIGNOF( VertexPTC );
    config.null = nullTexture;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_OFF;
    config.line_AA = NK_ANTI_ALIASING_OFF;

    const int MAX_VERTEX_MEMORY = 512 * 1024;
    const int MAX_ELEMENT_MEMORY = 128 * 1024;

    void* vertices;
    void* elements;
    System::MapUIVertexBuffer( MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, &vertices, &elements );
    memset( vertices, 0, MAX_VERTEX_MEMORY * sizeof( VertexPTC ) );
    memset( elements, 0, MAX_ELEMENT_MEMORY * 3 * 2 );

    nk_buffer vbuf, ebuf;
    nk_buffer_init_fixed( &vbuf, vertices, MAX_VERTEX_MEMORY );
    nk_buffer_init_fixed( &ebuf, elements, MAX_ELEMENT_MEMORY );
    nk_convert( ctx, uiCommands, &vbuf, &ebuf, &config );
#if RENDERER_VULKAN
    for (unsigned i = 0; i < MAX_VERTEX_MEMORY / (unsigned)sizeof( VertexPTC ); ++i)
    {
        VertexPTC* vert = &((VertexPTC*)vertices)[ i ];
        vert->position[ 1 ] = height - vert->position[ 1 ];
    }
#endif

    System::UnmapUIVertexBuffer();

    const nk_draw_command* cmd = nullptr;
    int offset = 0;

    nk_draw_foreach( cmd, ctx, uiCommands )
    {
        if (cmd->elem_count == 0)
        {
            continue;
        }

        System::DrawUI( (int)(cmd->clip_rect.x),
#if RENDERER_VULKAN
        ( int )(cmd->clip_rect.y - cmd->clip_rect.h),
#else
            (int)((height - (int)(cmd->clip_rect.y + cmd->clip_rect.h))),
#endif
            (int)(cmd->clip_rect.w),
            (int)(cmd->clip_rect.h),
            cmd->elem_count, uiTextures[ 0/*cmd->texture.id*/ ], offset, width, height );
        offset += cmd->elem_count / 3;
    }

    nk_clear( ctx );
}


Scene scene;
GameObject camera;

void sceneRenderFunc( int eye )
{
    VR::CalcCameraForEye( camera, 0, eye );
    scene.Render();
}

int main()
{
    bool fullScreen = false;

    int originalWidth = 1920 / 1;
    int originalHeight = 1080 / 1;
    int width = originalWidth;
    int height = originalHeight;

    if (fullScreen)
    {
        width = 0;
        height = 0;
    }
    
    System::EnableWindowsMemleakDetection();
    Window::Create( width, height, fullScreen ? WindowCreateFlags::Fullscreen : WindowCreateFlags::MSAA4 );
    int realHeight = 0;
    Window::GetSize( width, realHeight );
    
#if RENDERER_VULKAN
    int heightDelta = 0;
#else
    int heightDelta = (height - realHeight) / 2;
#endif
    height = realHeight;

    if (fullScreen)
    {
        originalWidth = width;
        originalHeight = height;
    }

    Window::SetTitle( "PBRSample" );
    System::LoadBuiltinAssets();
    System::InitAudio();
    System::InitGamePad();

    RenderTexture cameraTex;
    cameraTex.Create2D( width, height, RenderTexture::DataType::Float, TextureWrap::Clamp, TextureFilter::Linear, "cameraTex" );

    Texture2D bloomTex;
    bloomTex.CreateUAV( width / 2, height / 2, "bloomTex" );

	Texture2D blurTex;
	blurTex.CreateUAV( width / 2, height / 2, "blurTex" );

    RenderTexture resolvedTex;
    resolvedTex.Create2D( width, height, RenderTexture::DataType::Float, TextureWrap::Clamp, TextureFilter::Linear, "resolve" );
        
    RenderTexture camera2dTex;
    camera2dTex.Create2D( width, height, RenderTexture::DataType::Float, TextureWrap::Clamp, TextureFilter::Linear, "camera2dTex" );

    nk_context ctx;
    nk_font_atlas atlas;
    int atlasWidth = 0;
    int atlasHeight = 0;
    Texture2D nkFontTexture;
    nk_buffer cmds;

    nk_font_atlas_init_default( &atlas );
    nk_font_atlas_begin( &atlas );

    nk_font* nkFont = nk_font_atlas_add_default( &atlas, 13.0f, nullptr );
    const void* image = nk_font_atlas_bake( &atlas, &atlasWidth, &atlasHeight, NK_FONT_ATLAS_RGBA32 );

#if RENDERER_VULKAN
    nkFontTexture.LoadFromData( image, atlasWidth, atlasHeight, 4, "Nuklear font", VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
#else
    nkFontTexture.LoadFromData( image, atlasWidth, atlasHeight, 4, "Nuklear font" );
#endif
    nk_font_atlas_end( &atlas, nk_handle_id( nkFontTexture.GetID() ), &nullTexture );

    uiTextures[ 0 /*nk_handle_id( nkFontTexture.GetID() ).id*/ ] = &nkFontTexture;

    nk_init_default( &ctx, &nkFont->handle );
    nk_buffer_init_default( &cmds );

    camera.AddComponent<CameraComponent>();
    camera.GetComponent<CameraComponent>()->SetClearColor( Vec3( 0, 0, 0 ) );
    camera.GetComponent<CameraComponent>()->SetProjectionType( CameraComponent::ProjectionType::Perspective );
    camera.GetComponent<CameraComponent>()->SetProjection( 45, (float)originalWidth / (float)originalHeight, 0.1f, 200 );
    camera.GetComponent<CameraComponent>()->GetDepthNormalsTexture().Create2D( originalWidth, originalHeight, ae3d::RenderTexture::DataType::Float, ae3d::TextureWrap::Clamp, ae3d::TextureFilter::Nearest, "depthnormals" );
    camera.GetComponent<CameraComponent>()->SetClearFlag( CameraComponent::ClearFlag::DepthAndColor );
    camera.GetComponent<CameraComponent>()->SetRenderOrder( 1 );
    camera.GetComponent<CameraComponent>()->SetTargetTexture( &cameraTex );
    //camera.GetComponent<CameraComponent>()->SetViewport( 0, 0, originalWidth / 2, originalHeight );
    camera.AddComponent<TransformComponent>();
    camera.GetComponent<TransformComponent>()->LookAt( { 0, 0, -80 }, { 0, 0, 100 }, { 0, 1, 0 } );
    camera.SetName( "camera" );
    
    GameObject camera2d;
    camera2d.AddComponent<CameraComponent>();
    camera2d.GetComponent<CameraComponent>()->SetClearColor( Vec3( 0, 0, 0 ) );
    camera2d.GetComponent<CameraComponent>()->SetProjectionType( CameraComponent::ProjectionType::Orthographic );
    camera2d.GetComponent<CameraComponent>()->SetProjection( 0, (float)width, (float)height, 0, 0, 1 );
    camera2d.GetComponent<CameraComponent>()->SetClearFlag( CameraComponent::ClearFlag::DepthAndColor );
    camera2d.GetComponent<CameraComponent>()->SetLayerMask( 0x2 );
    camera2d.GetComponent<CameraComponent>()->SetTargetTexture( &camera2dTex );
    camera2d.GetComponent<CameraComponent>()->SetRenderOrder( 2 );
    camera2d.AddComponent<TransformComponent>();
    camera2d.SetName( "camera2d" );
    
    Texture2D fontTex;
    fontTex.Load( FileSystem::FileContents( "font.png" ), TextureWrap::Clamp, TextureFilter::Linear, Mipmaps::None, ColorSpace::SRGB, Anisotropy::k1 );

    Texture2D normalTex;
    normalTex.Load( FileSystem::FileContents( "textures/default_n.png" ), TextureWrap::Repeat, TextureFilter::Linear, Mipmaps::Generate, ColorSpace::Linear, Anisotropy::k1 );

    Texture2D whiteTex;
    whiteTex.Load( FileSystem::FileContents( "default_white.png" ), TextureWrap::Repeat, TextureFilter::Linear, Mipmaps::Generate, ColorSpace::SRGB, Anisotropy::k1 );
    
    Font font;
    font.LoadBMFont( &fontTex, FileSystem::FileContents( "font_txt.fnt" ) );

    GameObject statsContainer;
    statsContainer.AddComponent<TextRendererComponent>();
    statsContainer.GetComponent<TextRendererComponent>()->SetText( "Aether3D \nGame Engine" );
    statsContainer.GetComponent<TextRendererComponent>()->SetFont( &font );
    statsContainer.AddComponent<TransformComponent>();
    statsContainer.GetComponent<TransformComponent>()->SetLocalPosition( { 20, 40, 0 } );
    //statsContainer.GetComponent<TransformComponent>()->SetLocalScale( 0.5f );
    statsContainer.SetLayer( 2 );
    
    Mesh cubeMesh;
    cubeMesh.Load( FileSystem::FileContents( "textured_cube.ae3d" ) );

    GameObject cube;
    cube.AddComponent< MeshRendererComponent >();
    cube.GetComponent< MeshRendererComponent >()->SetMesh( &cubeMesh );
    cube.AddComponent< TransformComponent >();
    cube.GetComponent< TransformComponent >()->SetLocalPosition( { 0, 4, -80 } );

    Shader shader;
    shader.Load( "unlitVert", "unlitFrag",
                 FileSystem::FileContents( "unlit_vert.obj" ), FileSystem::FileContents( "unlit_frag.obj" ),
                 FileSystem::FileContents( "unlit_vert.spv" ), FileSystem::FileContents( "unlit_frag.spv" ) );

    Shader shaderSkin;
    shaderSkin.Load( "unlitVert", "unlitFrag",
                FileSystem::FileContents( "unlit_skin_vert.obj" ), FileSystem::FileContents( "unlit_frag.obj" ),
                FileSystem::FileContents( "unlit_skin_vert.spv" ), FileSystem::FileContents( "unlit_frag.spv" ) );
#ifdef TEST_BLOOM
    ComputeShader blurShader;
	blurShader.Load( "blur", FileSystem::FileContents( "Blur.obj" ), FileSystem::FileContents( "Blur.spv" ) );

	ComputeShader downsampleAndThresholdShader;
	downsampleAndThresholdShader.Load( "downsampleAndThreshold", FileSystem::FileContents( "Bloom.obj" ), FileSystem::FileContents( "Bloom.spv" ) );
#endif
    
    Texture2D gliderTex;
    gliderTex.Load( FileSystem::FileContents( "glider.png" ), TextureWrap::Repeat, TextureFilter::Linear, Mipmaps::Generate, ColorSpace::SRGB, Anisotropy::k1 );

    Material material;
    material.SetShader( &shader );
    material.SetTexture( &gliderTex, 0 );
    material.SetBackFaceCulling( true );

    cube.GetComponent< MeshRendererComponent >()->SetMaterial( &material, 0 );

    Shader shaderCubeMap;
    shaderCubeMap.Load( "unlitVert", "unlitFrag",
                        FileSystem::FileContents( "unlit_cube_vert.obj" ), FileSystem::FileContents( "unlit_cube_frag.obj" ),
                        FileSystem::FileContents( "unlit_cube_vert.spv" ), FileSystem::FileContents( "unlit_cube_frag.spv" ) );

    GameObject pointLight;
    pointLight.AddComponent<PointLightComponent>();
    pointLight.GetComponent<PointLightComponent>()->SetRadius( 1 );
    pointLight.AddComponent<TransformComponent>();
    pointLight.GetComponent<TransformComponent>()->SetLocalPosition( { 2, 0, -98 } );
    
    scene.SetAmbient( { 0.1f, 0.1f, 0.1f } );
    
    TextureCube skybox;
    skybox.Load( FileSystem::FileContents( "skybox/left.jpg" ), FileSystem::FileContents( "skybox/right.jpg" ),
                 FileSystem::FileContents( "skybox/bottom.jpg" ), FileSystem::FileContents( "skybox/top.jpg" ),
                 FileSystem::FileContents( "skybox/front.jpg" ), FileSystem::FileContents( "skybox/back.jpg" ),
                 TextureWrap::Clamp, TextureFilter::Linear, Mipmaps::None, ColorSpace::SRGB );
    /*const char* path = "test_dxt1.dds";
    skybox.Load( FileSystem::FileContents( path ), FileSystem::FileContents( path ),
        FileSystem::FileContents( path ), FileSystem::FileContents( path ),
        FileSystem::FileContents( path ), FileSystem::FileContents( path ),
        TextureWrap::Clamp, TextureFilter::Linear, Mipmaps::Generate, ColorSpace::SRGB );*/
    Shader standardShader;
    standardShader.Load( "standard_vertex", "standard_fragment",
        ae3d::FileSystem::FileContents( "Standard_vert.obj" ), ae3d::FileSystem::FileContents( "Standard_frag.obj" ),
        ae3d::FileSystem::FileContents( "Standard_vert.spv" ), ae3d::FileSystem::FileContents( "Standard_frag.spv" ) );

    Material standardMaterial;
    standardMaterial.SetShader( &standardShader );
    standardMaterial.SetTexture( &gliderTex, 0 );
    standardMaterial.SetTexture( &skybox );
    
    scene.SetSkybox( &skybox );
    scene.Add( &camera );
    scene.Add( &camera2d );
    scene.Add( &statsContainer );
    //scene.Add( &dirLight );

    bool quit = false;
    
    int lastMouseX = 0;
    int lastMouseY = 0;
    
    float yaw = 0;
    float gamePadLeftThumbX = 0;
    float gamePadLeftThumbY = 0;
    float gamePadRightThumbX = 0;
    float gamePadRightThumbY = 0;
    
    float angle = 0;
    Vec3 moveDir;

    bool reload = false;

    while (Window::IsOpen() && !quit)
    {
        Window::PumpEvents();
        WindowEvent event;
        
        ++angle;
        Quaternion rotation;
        Vec3 axis( 0, 1, 0 );
        rotation.FromAxisAngle( axis, angle );

        nk_input_begin( &ctx );

        while (Window::PollEvent( event ))
        {
            if (event.type == WindowEventType::Close)
            {
                quit = true;
            }
            else if (event.type == WindowEventType::KeyDown)
            {
                KeyCode keyCode = event.keyCode;
                
                const float velocity = 0.3f;
                
                if (keyCode == KeyCode::Escape)
                {
                    quit = true;
                }
                else if (keyCode == KeyCode::W)
                {
                    moveDir.z = -velocity;
                }
                else if (keyCode == KeyCode::S)
                {
                    moveDir.z = velocity;
                }
                else if (keyCode == KeyCode::E)
                {
                    moveDir.y = velocity;
                }
                else if (keyCode == KeyCode::Q)
                {
                    moveDir.y = -velocity;
                }
                else if (keyCode == KeyCode::A)
                {
                    moveDir.x = -velocity;
                }
                else if (keyCode == KeyCode::D)
                {
                    moveDir.x = velocity;
                }
                else if (keyCode == KeyCode::Left)
                {
                    camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 0, 1, 0 ), 1 );
                    yaw += 4;
                }
                else if (keyCode == KeyCode::Right)
                {
                    camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 0, 1, 0 ), -1 );
                    yaw -= 4;
                }
                else if (keyCode == KeyCode::Up)
                {
                    camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 1, 0, 0 ), 1 );
                }
                else if (keyCode == KeyCode::Down)
                {
                    camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 1, 0, 0 ), -1 );
                }
            }
            else if (event.type == WindowEventType::KeyUp)
            {
                KeyCode keyCode = event.keyCode;

                if (keyCode == KeyCode::W)
                {
                    moveDir.z = 0;
                }
                else if (keyCode == KeyCode::S)
                {
                    moveDir.z = 0;
                }
                else if (keyCode == KeyCode::E)
                {
                    moveDir.y = 0;
                }
                else if (keyCode == KeyCode::Q)
                {
                    moveDir.y = 0;
                }
                else if (keyCode == KeyCode::A)
                {
                    moveDir.x = 0;
                }
                else if (keyCode == KeyCode::D)
                {
                    moveDir.x = 0;
                }
                else if (keyCode == KeyCode::R)
                {
                    reload = true;
                }
            }
            else if (event.type == WindowEventType::MouseMove)
            {
                const int mouseDeltaX = event.mouseX - lastMouseX;
                const int mouseDeltaY = event.mouseY - lastMouseY;
                lastMouseX = event.mouseX;
                lastMouseY = event.mouseY;
                camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 0, 1, 0 ), -float( mouseDeltaX ) / 20 );
                camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 1, 0, 0 ), float( mouseDeltaY ) / 20 );
  
                int x = event.mouseX;
                int y = height - event.mouseY + heightDelta;
                nk_input_motion( &ctx, (int)x, (int)y );
            }
            else if (event.type == WindowEventType::Mouse1Down)
            {
                int x = event.mouseX;
                int y = height - event.mouseY;

                nk_input_button( &ctx, NK_BUTTON_LEFT, x, y, 1 );
            }
            else if (event.type == WindowEventType::Mouse1Up)
            {
                int x = event.mouseX;
                int y = height - event.mouseY;

                nk_input_button( &ctx, NK_BUTTON_LEFT, x, y, 0 );
            }
        }

        nk_input_end( &ctx );

        camera.GetComponent<TransformComponent>()->MoveUp( moveDir.y );
        camera.GetComponent<TransformComponent>()->MoveForward( moveDir.z );
        camera.GetComponent<TransformComponent>()->MoveRight( moveDir.x );

        camera.GetComponent<TransformComponent>()->MoveForward( -gamePadLeftThumbY );
        camera.GetComponent<TransformComponent>()->MoveRight( gamePadLeftThumbX );
        camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 0, 1, 0 ), -float( gamePadRightThumbX ) / 1 );
        camera.GetComponent<TransformComponent>()->OffsetRotate( Vec3( 1, 0, 0 ), float( gamePadRightThumbY ) / 1 );

        if (reload)
        {
            System::Print("reloading\n");
            System::ReloadChangedAssets();
            reload = false;
        }

        static float value = 0.6f;

        if (nk_begin( &ctx, "Demo", nk_rect( 0, 50, 300, 400 ), NK_WINDOW_BORDER | NK_WINDOW_TITLE ))
        {
            nk_layout_row_static( &ctx, 30, 80, 1 );

            nk_layout_row_dynamic( &ctx, 30, 2 );
            enum { EASY, HARD };
            static int op = EASY;
            if (nk_option_label( &ctx, "easy", op == EASY )) op = EASY;
            if (nk_option_label( &ctx, "hard", op == HARD )) op = HARD;
            nk_check_label( &ctx, "check1", 0 );
            nk_check_label( &ctx, "check2", 1 );
            static size_t prog_value = 54;
            nk_progress( &ctx, &prog_value, 100, NK_MODIFIABLE );

            static float pos;

            nk_property_float( &ctx, "#X:", -1024.0f, &pos, 1024.0f, 1, 1 );

            static const char *weapons[] = { "Fist", "Pistol", "Shotgun" };
            static int current_weapon = 0;
            current_weapon = nk_combo( &ctx, weapons, 3, current_weapon, 25, nk_vec2( nk_widget_width( &ctx ), 200 ) );

            nk_layout_row_begin( &ctx, NK_STATIC, 30, 2 );
            {
                nk_layout_row_push( &ctx, 60 );
                nk_label( &ctx, "Roughness:", NK_TEXT_LEFT );
                nk_layout_row_push( &ctx, 110 );
                nk_slider_float( &ctx, 0, &value, 1.0f, 0.1f );
            }
            nk_layout_row_end( &ctx );

            nk_end( &ctx );
        }

        scene.Render();
        cameraTex.ResolveTo( &resolvedTex );
        System::Draw( &resolvedTex, 0, 0, width, height, width, height, Vec4( 1, 1, 1, 1 ), System::BlendMode::Off );
        DrawNuklear( &ctx, &cmds, width, height );
        scene.EndFrame();
        Window::SwapBuffers();
    }

    VR::Deinit();
    System::Deinit();
}
