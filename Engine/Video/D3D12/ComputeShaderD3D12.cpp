// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com
#include "ComputeShader.hpp"
#include <d3d12.h>
#include <d3dcompiler.h>
#include "FileSystem.hpp"
#include "GfxDevice.hpp"
#include "Macros.hpp"
#include "System.hpp"
#include "TextureBase.hpp"
#include "Texture2D.hpp"

extern int AE3D_CB_SIZE;

void TransitionResource( GpuResource& gpuResource, D3D12_RESOURCE_STATES newState );

namespace GfxDeviceGlobal
{
    extern ID3D12Device* device;
    extern ID3D12GraphicsCommandList* graphicsCommandList;
    extern ID3D12RootSignature* rootSignatureTileCuller;
    extern ID3D12DescriptorHeap* computeCbvSrvUavHeaps[ 3 ];
    extern D3D12_UNORDERED_ACCESS_VIEW_DESC uav1Desc;
    extern ID3D12PipelineState* cachedPSO;
	extern PerObjectUboStruct perObjectUboStruct;
    extern ID3D12Resource* uav1;
}

namespace Global
{
    std::vector< ID3DBlob* > computeShaders;
    std::vector< ID3D12PipelineState* > psos;
}

void DestroyComputeShaders()
{
    for (std::size_t i = 0; i < Global::computeShaders.size(); ++i)
    {
        AE3D_SAFE_RELEASE( Global::computeShaders[ i ] );
    }

    for (std::size_t i = 0; i < Global::psos.size(); ++i)
    {
        AE3D_SAFE_RELEASE( Global::psos[ i ] );
    }
}

void ae3d::ComputeShader::Begin()
{
}

void ae3d::ComputeShader::End()
{
}

void ae3d::ComputeShader::SetUniform( UniformName uniform, float x, float y )
{
    if( uniform == UniformName::TilesZW )
    {
        GfxDeviceGlobal::perObjectUboStruct.tilesXY.z = x;
        GfxDeviceGlobal::perObjectUboStruct.tilesXY.w = y;
    }
}

void ae3d::ComputeShader::Dispatch( unsigned groupCountX, unsigned groupCountY, unsigned groupCountZ )
{
    static int heapIndex = 0;
    heapIndex = (heapIndex + 1) % 3;
    System::Assert( GfxDeviceGlobal::graphicsCommandList != nullptr, "graphics command list not initialized" );
    System::Assert( GfxDeviceGlobal::computeCbvSrvUavHeaps[ heapIndex ] != nullptr, "heap not initialized" );

    if (!pso)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
        descPso.CS = { reinterpret_cast<BYTE*>(blobShader->GetBufferPointer()), blobShader->GetBufferSize() };
        descPso.pRootSignature = GfxDeviceGlobal::rootSignatureTileCuller;

        HRESULT hr = GfxDeviceGlobal::device->CreateComputePipelineState( &descPso, IID_PPV_ARGS( &pso ) );
        AE3D_CHECK_D3D( hr, "Failed to create compute PSO" );
        pso->SetName( L"PSO" );
        Global::psos.push_back( pso );
    }

    SetCBV( 0, (ID3D12Resource*)GfxDevice::GetCurrentConstantBuffer() );

    D3D12_CPU_DESCRIPTOR_HANDLE handle = GfxDeviceGlobal::computeCbvSrvUavHeaps[ heapIndex ]->GetCPUDescriptorHandleForHeapStart();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = uniformBuffers[ 0 ]->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = AE3D_CB_SIZE;
    GfxDeviceGlobal::device->CreateConstantBufferView( &cbvDesc, handle );

    handle.ptr += GfxDeviceGlobal::device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    GfxDeviceGlobal::device->CreateShaderResourceView( textureBuffers[ 0 ], &srvDescs[ 0 ], handle );

    handle.ptr += GfxDeviceGlobal::device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    GfxDeviceGlobal::device->CreateShaderResourceView( textureBuffers[ 1 ], &srvDescs[ 1 ], handle );

    handle.ptr += GfxDeviceGlobal::device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    GfxDeviceGlobal::device->CreateShaderResourceView( textureBuffers[ 2 ], &srvDescs[ 2 ], handle );

    handle.ptr += GfxDeviceGlobal::device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    GfxDeviceGlobal::device->CreateUnorderedAccessView( uavBuffers[ 0 ], nullptr, &GfxDeviceGlobal::uav1Desc, handle );

    GpuResource depthNormals = {};
    depthNormals.resource = textureBuffers[ 1 ];
    depthNormals.usageState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (depthNormals.resource != nullptr)
    {
        TransitionResource( depthNormals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
    }

    GfxDeviceGlobal::cachedPSO = pso;
    GfxDeviceGlobal::graphicsCommandList->SetPipelineState( pso );
    GfxDeviceGlobal::graphicsCommandList->SetDescriptorHeaps( 1, &GfxDeviceGlobal::computeCbvSrvUavHeaps[ heapIndex ] );
    GfxDeviceGlobal::graphicsCommandList->SetComputeRootSignature( GfxDeviceGlobal::rootSignatureTileCuller );
    GfxDeviceGlobal::graphicsCommandList->SetComputeRootDescriptorTable( 0, GfxDeviceGlobal::computeCbvSrvUavHeaps[ heapIndex ]->GetGPUDescriptorHandleForHeapStart() );
    GfxDeviceGlobal::graphicsCommandList->Dispatch( groupCountX, groupCountY, groupCountZ );

    if (depthNormals.resource != nullptr)
    {
        TransitionResource( depthNormals, D3D12_RESOURCE_STATE_RENDER_TARGET );
    }
}

void ae3d::ComputeShader::Load( const char* source )
{
    const std::size_t sourceLength = std::string( source ).size();
    ID3DBlob* blobError = nullptr;
    HRESULT hr = D3DCompile( source, sourceLength, "CSMain", nullptr /*defines*/, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSMain", "cs_5_1",
#if DEBUG
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blobShader, &blobError );
#else
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blobShader, &blobError );
#endif
    if (FAILED( hr ))
    {
        ae3d::System::Print( "Unable to compile compute shader: %s!\n", blobError->GetBufferPointer() );
        blobError->Release();
        return;
    }

    Global::computeShaders.push_back( blobShader );
}

void ae3d::ComputeShader::Load( const char* /*metalShaderName*/, const FileSystem::FileContentsData& dataHLSL, const FileSystem::FileContentsData& /*dataSPIRV*/ )
{
    for (int slotIndex = 0; slotIndex < SLOT_COUNT; ++slotIndex)
    {
        uniformBuffers[ slotIndex ] = nullptr;
        textureBuffers[ slotIndex ] = nullptr;
        uavBuffers[ slotIndex ] = nullptr;
    }

    if (dataHLSL.path.find( ".obj" ) != std::string::npos)
    {
        wchar_t wstr[ 256 ];
        std::mbstowcs( wstr, dataHLSL.path.c_str(), 256 );

        HRESULT hr = D3DReadFileToBlob( wstr, &blobShader );
        AE3D_CHECK_D3D( hr, "Shader bytecode reading failed!" );
        
        Global::computeShaders.push_back( blobShader );
    }
    else
    {
        const std::string dataStr = std::string( std::begin( dataHLSL.data ), std::end( dataHLSL.data ) );
        Load( dataStr.c_str() );
    }
}

void ae3d::ComputeShader::SetRenderTexture( unsigned slot, RenderTexture* renderTexture )
{
    if (slot < SLOT_COUNT)
    {
        renderTextures[ slot ] = renderTexture;
    }
    else
    {
        System::Print( "SetRenderTexture: too high slot, max is 3\n" );
    }
}

void ae3d::ComputeShader::SetCBV( unsigned slot, ID3D12Resource* buffer )
{
    if (slot < SLOT_COUNT)
    {
        uniformBuffers[ slot ] = buffer;
    }
    else
    {
        System::Print( "SetCBV: too high slot, max is 3\n" );
    }
}

void ae3d::ComputeShader::SetSRV( unsigned slot, ID3D12Resource* buffer, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc )
{
    if (slot < SLOT_COUNT)
    {
        textureBuffers[ slot ] = buffer;
        srvDescs[ slot ] = srvDesc;
    }
    else
    {
        System::Print( "SetSRV: too high slot, max is 3\n" );
    }
}

void ae3d::ComputeShader::SetUAV( unsigned slot, ID3D12Resource* buffer, const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc )
{
    if (slot < SLOT_COUNT)
    {
        uavBuffers[ slot ] = buffer;
        GfxDeviceGlobal::uav1Desc = uavDesc;
        GfxDeviceGlobal::uav1 = buffer;
    }
    else
    {
        System::Print( "SetUAV: too high slot, max is 3\n" );
    }
}
