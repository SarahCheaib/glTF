// Copyright (c) 2012, Motorola Mobility, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the Motorola Mobility, Inc. nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef __JSON_MESH_H__
#define __JSON_MESH_H__

namespace GLTF 
{
    class GLTFMesh;
    
    shared_ptr <GLTFMesh> createUnifiedIndexesMeshFromMesh(GLTFMesh *sourceMesh, std::vector< shared_ptr<IndicesVector> > &vectorOfIndicesVector);
    
    typedef std::map<unsigned int /* IndexSet */, shared_ptr<GLTF::GLTFMeshAttribute> > IndexSetToMeshAttributeHashmap;
    typedef std::map<GLTF::Semantic , IndexSetToMeshAttributeHashmap > SemanticToMeshAttributeHashmap;
    
    class GLTFMesh {
    public:
        GLTFMesh();
        GLTFMesh(const GLTFMesh &mesh);
        
        virtual ~GLTFMesh();
        
        shared_ptr <MeshAttributeVector> meshAttributes();
        
        bool appendPrimitive(shared_ptr <GLTF::GLTFPrimitive> primitive);
        
        void setMeshAttributesForSemantic(GLTF::Semantic semantic, IndexSetToMeshAttributeHashmap& indexSetToMeshAttributeHashmap);        
        IndexSetToMeshAttributeHashmap& getMeshAttributesForSemantic(Semantic semantic); 
        
        std::vector <GLTF::Semantic> allSemantics();
                
        std::string getID();
        void setID(std::string ID);
        
        std::string getName();
        void setName(std::string name);
        
        PrimitiveVector const getPrimitives();

        bool writeAllBuffers(std::ofstream& verticesOutputStream, std::ofstream& indicesOutputStream);
        
    private:
        PrimitiveVector _primitives;
        SemanticToMeshAttributeHashmap _semanticToMeshAttributes;
        std::string _ID, _name;
    };    

}


#endif