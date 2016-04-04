#version 450 core
    
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

layout (binding = 0) uniform UBO 
{
    uniform mat4 _ProjectionModelMatrix;
} ubo;

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) out vec4 vColor;
    
void main()
{
    gl_Position = ubo._ProjectionModelMatrix * vec4( aPosition.xyz, 1.0 );
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
    vTexCoord = aTexCoord;
    vColor = aColor;
}
    