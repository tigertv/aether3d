#ifndef MESH_RENDERER_COMPONENT
#define MESH_RENDERER_COMPONENT

#include <string>
#include <vector>

namespace ae3d
{
    class Material;
    class Mesh;
    class Frustum;
    struct Matrix44;
    
    /// Contains a mesh.
    class MeshRendererComponent
    {
    public:
        /// \return Mesh.
        Mesh* GetMesh() const { return mesh; }
        
        /// \param material Material.
        /// \param subMeshIndex Sub mesh index.
        void SetMaterial( Material* material, int subMeshIndex );

        /// \param aMesh Mesh.
        void SetMesh( Mesh* aMesh );

        /// \return Textual representation of component.
        std::string GetSerialized() const;
        
    private:
        friend class GameObject;
        friend class Scene;
        
        /// \return Component's type code. Must be unique for each component type.
        static int Type() { return 5; }
        
        /// \return Component handle that uniquely identifies the instance.
        static unsigned New();
        
        /// \return Component at index or null if index is invalid.
        static MeshRendererComponent* Get( unsigned index );
        
        /// \param modelViewProjectionMatrix Model-view-projection matrix.
        /// \param cameraFrustum Camera frustum.
        /// \param localToWorld Transforms mesh AABB from mesh-local space into world-space.
        void Render( const Matrix44& modelViewProjectionMatrix, const Frustum& cameraFrustum, const Matrix44& localToWorld );

        Mesh* mesh = nullptr;
        std::vector< Material* > materials;
    };
}

#endif