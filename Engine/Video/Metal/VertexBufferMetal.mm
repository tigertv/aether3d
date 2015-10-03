#include "VertexBuffer.hpp"
#include "GfxDevice.hpp"

void ae3d::VertexBuffer::Bind() const
{
}

void ae3d::VertexBuffer::Generate( const Face* faces, int faceCount, const VertexPTC* vertices, int vertexCount )
{
    if (faceCount > 0)
    {
        vertexBuffer = [GfxDevice::GetMetalDevice() newBufferWithBytes:vertices
                                                 length:sizeof( VertexPTC ) * vertexCount
                                                options:MTLResourceOptionCPUCacheModeDefault];
        vertexBuffer.label = @"Vertex buffer";

        indexBuffer = [GfxDevice::GetMetalDevice() newBufferWithBytes:faces
                           length:sizeof( Face ) * faceCount
                          options:MTLResourceOptionCPUCacheModeDefault];
        indexBuffer.label = @"Index buffer";

        elementCount = faceCount * 3;
    }
}

void ae3d::VertexBuffer::Generate( const Face* faces, int faceCount, const VertexPTN* vertices, int vertexCount )
{
    if (faceCount > 0)
    {
        vertexBuffer = [GfxDevice::GetMetalDevice() newBufferWithBytes:vertices
                           length:sizeof( VertexPTN ) * vertexCount
                          options:MTLResourceOptionCPUCacheModeDefault];
        vertexBuffer.label = @"Vertex buffer";
        
        indexBuffer = [GfxDevice::GetMetalDevice() newBufferWithBytes:faces
                          length:sizeof( Face ) * faceCount
                         options:MTLResourceOptionCPUCacheModeDefault];
        indexBuffer.label = @"Index buffer";
        
        elementCount = faceCount * 3;
    }
}

void ae3d::VertexBuffer::Generate( const Face* faces, int faceCount, const VertexPTNTC* vertices, int vertexCount )
{
    if (faceCount > 0)
    {
        vertexBuffer = [GfxDevice::GetMetalDevice() newBufferWithBytes:vertices
                           length:sizeof( VertexPTNTC ) * vertexCount
                          options:MTLResourceOptionCPUCacheModeDefault];
        vertexBuffer.label = @"Vertex buffer";
        
        indexBuffer = [GfxDevice::GetMetalDevice() newBufferWithBytes:faces
                          length:sizeof( Face ) * faceCount
                         options:MTLResourceOptionCPUCacheModeDefault];
        indexBuffer.label = @"Index buffer";
        
        elementCount = faceCount * 3;
    }
}
