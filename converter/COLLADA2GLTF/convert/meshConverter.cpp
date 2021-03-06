#include "GLTF.h"
#include "../GLTF-OpenCOLLADA.h"
#include "../GLTFConverterContext.h"

#include "meshConverter.h"
#include "../helpers/mathHelpers.h"
#include "../helpers/geometryHelpers.h"

namespace GLTF
{
    static unsigned int ConvertOpenCOLLADAMeshVertexDataToGLTFMeshAttributes(const COLLADAFW::MeshVertexData &vertexData, GLTF::IndexSetToMeshAttributeHashmap &meshAttributes)
    {
        // The following are OpenCOLLADA fmk issues preventing doing a totally generic processing of sources
        //1. "set"(s) other than texCoord don't have valid input infos
        //2. not the original id in the source
        
        std::string name;
        size_t length, elementsCount;
        size_t stride = 0;
        size_t size = 0;
        size_t byteOffset = 0;
        size_t inputLength = 0;
        
        size_t setCount = vertexData.getNumInputInfos();
        bool unpatchedOpenCOLLADA = (setCount == 0); // reliable heuristic to know if the input have not been set
        
        if (unpatchedOpenCOLLADA)
            setCount = 1;
        
        for (size_t indexOfSet = 0 ; indexOfSet < setCount ; indexOfSet++) {
            
            if (!unpatchedOpenCOLLADA) {
                name = vertexData.getName(indexOfSet);
                size = vertexData.getStride(indexOfSet);
                inputLength = vertexData.getLength(indexOfSet);
            } else {
                // for unpatched version of OpenCOLLADA we need this work-around.
                name = GLTF::GLTFUtils::generateIDForType("buffer").c_str();
                size = 3; //only normal and positions should reach this code
                inputLength = vertexData.getLength(0);
            }
            
            //name is the id
            length = inputLength ? inputLength : vertexData.getValuesCount();
            elementsCount = length / size;
            unsigned char *sourceData = 0;
            size_t sourceSize = 0;
            
            GLTF::ComponentType componentType = GLTF::NOT_AN_ELEMENT_TYPE;
            switch (vertexData.getType()) {
                case COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT: {
                    componentType = GLTF::FLOAT;
                    stride = sizeof(float) * size;
                    const COLLADAFW::FloatArray* array = vertexData.getFloatValues();
                    
                    sourceData = (unsigned char*)array->getData() + byteOffset;
                    
                    sourceSize = length * sizeof(float);
                    byteOffset += sourceSize; //Doh! - OpenCOLLADA store all sets contiguously in the same array
                }
                    break;
                case COLLADAFW::MeshVertexData::DATA_TYPE_DOUBLE: {
                    /*
                     sourceType = DOUBLE;
                     
                     const DoubleArray& array = vertexData.getDoubleValues()[indexOfSet];
                     const size_t count = array.getCount();
                     sourceData = (void*)array.getData();
                     sourceSize = count * sizeof(double);
                     */
                    // Warning if can't make "safe" conversion
                }
                    
                    break;
                default:
                case COLLADAFW::MeshVertexData::DATA_TYPE_UNKNOWN:
                    //FIXME report error
                    break;
            }
            
            // FIXME: the source could be shared, store / retrieve it here
            shared_ptr <GLTFBufferView> cvtBufferView = createBufferViewWithAllocatedBuffer(name, sourceData, 0, sourceSize, false);
            shared_ptr <GLTFMeshAttribute> cvtMeshAttribute(new GLTFMeshAttribute());
            
            cvtMeshAttribute->setBufferView(cvtBufferView);
            cvtMeshAttribute->setComponentsPerAttribute(size);
            cvtMeshAttribute->setByteStride(stride);
            cvtMeshAttribute->setComponentType(componentType);
            cvtMeshAttribute->setCount(elementsCount);
            
            meshAttributes[(unsigned int)indexOfSet] = cvtMeshAttribute;
        }
        
        return (unsigned int)setCount;
    }
    
    static void __AppendIndices(shared_ptr <GLTF::GLTFPrimitive> &primitive, IndicesVector &primitiveIndicesVector, shared_ptr <GLTF::GLTFIndices> &indices, GLTF::Semantic semantic, unsigned int indexOfSet)
    {
        primitive->appendVertexAttribute(shared_ptr <GLTF::JSONVertexAttribute>( new GLTF::JSONVertexAttribute(semantic,indexOfSet)));
        primitiveIndicesVector.push_back(indices);
    }
    
    static void __HandleIndexList(unsigned int idx,
                                  COLLADAFW::IndexList *indexList,
                                  Semantic semantic,
                                  bool shouldTriangulate,
                                  unsigned int count,
                                  unsigned int vcount,
                                  unsigned int *verticesCountArray,
                                  shared_ptr <GLTF::GLTFPrimitive> cvtPrimitive,
                                  IndicesVector &primitiveIndicesVector
                                  )
    {
        unsigned int triangulatedIndicesCount = 0;
        bool ownData = false;
        unsigned int *indices = indexList->getIndices().getData();
        
        if (shouldTriangulate) {
            indices = createTrianglesFromPolylist(verticesCountArray, indices, vcount, &triangulatedIndicesCount);
            count = triangulatedIndicesCount;
            ownData = true;
        }
        
        //Why is OpenCOLLADA doing this ? why adding an offset the indices ??
        //We need to offset it backward here.
        unsigned int initialIndex = indexList->getInitialIndex();
        if (initialIndex != 0) {
            unsigned int *bufferDestination = 0;
            if (!ownData) {
                bufferDestination = (unsigned int*)malloc(sizeof(unsigned int) * count);
                ownData = true;
            } else {
                bufferDestination = indices;
            }
            for (size_t idx = 0 ; idx < count ; idx++) {
                bufferDestination[idx] = indices[idx] - initialIndex;
            }
            indices = bufferDestination;
        }
        
        shared_ptr <GLTF::GLTFBufferView> uvBuffer = createBufferViewWithAllocatedBuffer(indices, 0, count * sizeof(unsigned int), ownData);
        
        //FIXME: Looks like for texcoord indexSet begin at 1, this is out of the sync with the index used in ConvertOpenCOLLADAMeshVertexDataToGLTFMeshAttributes that begins at 0
        //for now forced to 0, to be fixed for multi texturing.
        
        //unsigned int idx = (unsigned int)indexList->getSetIndex();
        
        shared_ptr <GLTFIndices> jsonIndices(new GLTFIndices(uvBuffer, count));
        __AppendIndices(cvtPrimitive, primitiveIndicesVector, jsonIndices, semantic, idx);
    }
    
    static shared_ptr <GLTF::GLTFPrimitive> ConvertOpenCOLLADAMeshPrimitive(
                                                                            COLLADAFW::MeshPrimitive *openCOLLADAMeshPrimitive,
                                                                            IndicesVector &primitiveIndicesVector)
    {
        shared_ptr <GLTF::GLTFPrimitive> cvtPrimitive(new GLTF::GLTFPrimitive());
        
        // We want to match OpenGL/ES mode , as WebGL spec points to OpenGL/ES spec...
        // "Symbolic constants GL_POINTS, GL_LINE_STRIP, GL_LINE_LOOP, GL_LINES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, and GL_TRIANGLES are accepted."
        std::string type;
        bool shouldTriangulate = false;
        
        switch(openCOLLADAMeshPrimitive->getPrimitiveType()) {
                //these 2 requires transforms
            case COLLADAFW::MeshPrimitive::POLYLIST:
            case COLLADAFW::MeshPrimitive::POLYGONS:
                // FIXME: perform conversion, but until not done report error
                //these mode are supported by WebGL
                shouldTriangulate = true;
                //force triangles
                type = "TRIANGLES";
                break;
            case  COLLADAFW::MeshPrimitive::LINES:
                type = "LINES";
                break;
            case  COLLADAFW::MeshPrimitive::LINE_STRIPS:
                type = "LINE_STRIP";
                break;
                
            case  COLLADAFW::MeshPrimitive::TRIANGLES:
                type = "TRIANGLES";
                break;
                
            case  COLLADAFW::MeshPrimitive::TRIANGLE_FANS:
                type = "TRIANGLE_FANS";
                break;
                
            case  COLLADAFW::MeshPrimitive::TRIANGLE_STRIPS:
                type = "TRIANGLE_STRIPS";
                break;
                
            case  COLLADAFW::MeshPrimitive::POINTS:
                type = "POINTS";
                break;
            default:
                break;
        }
        
        cvtPrimitive->setMaterialObjectID((unsigned int)openCOLLADAMeshPrimitive->getMaterialId());
        cvtPrimitive->setType(type);
        
        //count of indices , it must be the same for all kind of indices
        size_t count = openCOLLADAMeshPrimitive->getPositionIndices().getCount();
        
        //vertex
        //IndexList &positionIndexList = openCOLLADAMeshPrimitive->getPositionIndices();
        unsigned int *indices = openCOLLADAMeshPrimitive->getPositionIndices().getData();
        unsigned int *verticesCountArray = 0;
        unsigned int vcount = 0;   //count of elements in the array containing the count of indices per polygon & polylist.
        
        if (shouldTriangulate) {
            unsigned int triangulatedIndicesCount = 0;
            //We have to upcast to polygon to retrieve the array of vertexCount
            //OpenCOLLADA use polylist as polygon.
            COLLADAFW::Polygons *polygon = (COLLADAFW::Polygons*)openCOLLADAMeshPrimitive;
            const COLLADAFW::Polygons::VertexCountArray& vertexCountArray = polygon->getGroupedVerticesVertexCountArray();
            vcount = (unsigned int)vertexCountArray.getCount();
            verticesCountArray = (unsigned int*)malloc(sizeof(unsigned int) * vcount);
            for (size_t i = 0; i < vcount; i++) {
                verticesCountArray[i] = polygon->getGroupedVerticesVertexCount(i);;
            }
            indices = createTrianglesFromPolylist(verticesCountArray, indices, vcount, &triangulatedIndicesCount);
            count = triangulatedIndicesCount;
        }
        
        shared_ptr <GLTFBufferView> positionBuffer = createBufferViewWithAllocatedBuffer(indices, 0, count * sizeof(unsigned int), shouldTriangulate ? true : false);
        
        shared_ptr <GLTF::GLTFIndices> positionIndices(new GLTF::GLTFIndices(positionBuffer,count));
        
        __AppendIndices(cvtPrimitive, primitiveIndicesVector, positionIndices, POSITION, 0);
        
        if (openCOLLADAMeshPrimitive->hasNormalIndices()) {
            unsigned int triangulatedIndicesCount = 0;
            indices = openCOLLADAMeshPrimitive->getNormalIndices().getData();
            if (shouldTriangulate) {
                indices = createTrianglesFromPolylist(verticesCountArray, indices, vcount, &triangulatedIndicesCount);
                count = triangulatedIndicesCount;
            }
            
            shared_ptr <GLTF::GLTFBufferView> normalBuffer = createBufferViewWithAllocatedBuffer(indices, 0, count * sizeof(unsigned int), shouldTriangulate ? true : false);
            shared_ptr <GLTF::GLTFIndices> normalIndices(new GLTF::GLTFIndices(normalBuffer,
                                                                               count));
            __AppendIndices(cvtPrimitive, primitiveIndicesVector, normalIndices, NORMAL, 0);
        }
        
        if (openCOLLADAMeshPrimitive->hasColorIndices()) {
            COLLADAFW::IndexListArray& colorListArray = openCOLLADAMeshPrimitive->getColorIndicesArray();
            for (size_t i = 0 ; i < colorListArray.getCount() ; i++) {
                COLLADAFW::IndexList* indexList = openCOLLADAMeshPrimitive->getColorIndices(i);
                __HandleIndexList(i,
                                  indexList,
                                  GLTF::COLOR,
                                  shouldTriangulate,
                                  count,
                                  vcount,
                                  verticesCountArray,
                                  cvtPrimitive,
                                  primitiveIndicesVector);
            }
        }
        
        if (openCOLLADAMeshPrimitive->hasUVCoordIndices()) {
            COLLADAFW::IndexListArray& uvListArray = openCOLLADAMeshPrimitive->getUVCoordIndicesArray();
            for (size_t i = 0 ; i < uvListArray.getCount() ; i++) {
                COLLADAFW::IndexList* indexList = openCOLLADAMeshPrimitive->getUVCoordIndices(i);
                __HandleIndexList(i,
                                  indexList,
                                  GLTF::TEXCOORD,
                                  shouldTriangulate,
                                  count,
                                  vcount,
                                  verticesCountArray,
                                  cvtPrimitive,
                                  primitiveIndicesVector);
            }
        }
        
        if (verticesCountArray) {
            free(verticesCountArray);
        }
        
        return cvtPrimitive;
    }
    
    static void __InvertV(void *value,
                          GLTF::ComponentType type,
                          size_t componentsPerAttribute,
                          size_t index,
                          size_t vertexAttributeByteSize,
                          void *context) {
        char* bufferData = (char*)value;
        
        if (componentsPerAttribute > 1) {
            switch (type) {
                case GLTF::FLOAT: {
                    float* vector = (float*)bufferData;
                    vector[1] = (float) (1.0 - vector[1]);
                }
                    break;
                default:
                    break;
            }
        }
    }
    
    void convertOpenCOLLADAMesh(COLLADAFW::Mesh* openCOLLADAMesh,
                                MeshVector &meshes)
    {
        shared_ptr <GLTF::GLTFMesh> cvtMesh(new GLTF::GLTFMesh());
        
        cvtMesh->setID(openCOLLADAMesh->getOriginalId());
        cvtMesh->setName(openCOLLADAMesh->getName());
        
        const COLLADAFW::MeshPrimitiveArray& primitives =  openCOLLADAMesh->getMeshPrimitives();
        size_t primitiveCount = primitives.getCount();
        
        std::vector< shared_ptr<IndicesVector> > allPrimitiveIndicesVectors;
        
        // get all primitives
        for (size_t i = 0 ; i < primitiveCount ; i++) {
            const COLLADAFW::MeshPrimitive::PrimitiveType primitiveType = primitives[i]->getPrimitiveType();
            if ((primitiveType != COLLADAFW::MeshPrimitive::TRIANGLES) &&
                (primitiveType != COLLADAFW::MeshPrimitive::TRIANGLE_STRIPS) &&
                (primitiveType != COLLADAFW::MeshPrimitive::POLYLIST) &&
                (primitiveType != COLLADAFW::MeshPrimitive::POLYGONS))
                continue;
            
            shared_ptr <GLTF::IndicesVector> primitiveIndicesVector(new GLTF::IndicesVector());
            allPrimitiveIndicesVectors.push_back(primitiveIndicesVector);
            
            shared_ptr <GLTF::GLTFPrimitive> primitive = ConvertOpenCOLLADAMeshPrimitive(primitives[i],*primitiveIndicesVector);
            cvtMesh->appendPrimitive(primitive);
            
            VertexAttributeVector vertexAttributes = primitive->getVertexAttributes();
            primitiveIndicesVector = allPrimitiveIndicesVectors[i];
            
            // once we got a primitive, keep track of its meshAttributes
            std::vector< shared_ptr<GLTF::GLTFIndices> > allIndices = *primitiveIndicesVector;
            for (size_t k = 0 ; k < allIndices.size() ; k++) {
                shared_ptr<GLTF::GLTFIndices> indices = allIndices[k];
                GLTF::Semantic semantic = vertexAttributes[k]->getSemantic();
                GLTF::IndexSetToMeshAttributeHashmap& meshAttributes = cvtMesh->getMeshAttributesForSemantic(semantic);
                
                switch (semantic) {
                    case GLTF::POSITION:
                        ConvertOpenCOLLADAMeshVertexDataToGLTFMeshAttributes(openCOLLADAMesh->getPositions(), meshAttributes);
                        break;
                        
                    case GLTF::NORMAL:
                        ConvertOpenCOLLADAMeshVertexDataToGLTFMeshAttributes(openCOLLADAMesh->getNormals(), meshAttributes);
                        break;
                        
                    case GLTF::TEXCOORD:
                        ConvertOpenCOLLADAMeshVertexDataToGLTFMeshAttributes(openCOLLADAMesh->getUVCoords(), meshAttributes);
                        break;
                        
                    case GLTF::COLOR:
                        ConvertOpenCOLLADAMeshVertexDataToGLTFMeshAttributes(openCOLLADAMesh->getColors(), meshAttributes);
                        break;
                        
                    default:
                        break;
                }
                
                cvtMesh->setMeshAttributesForSemantic(semantic, meshAttributes);
            }
        }
        
        //https://github.com/KhronosGroup/collada2json/issues/41
        //Goes through all texcoord and invert V
        GLTF::IndexSetToMeshAttributeHashmap& texcoordMeshAttributes = cvtMesh->getMeshAttributesForSemantic(GLTF::TEXCOORD);
        GLTF::IndexSetToMeshAttributeHashmap::const_iterator meshAttributeIterator;
        
        //FIXME: consider turn this search into a method for mesh
        for (meshAttributeIterator = texcoordMeshAttributes.begin() ; meshAttributeIterator != texcoordMeshAttributes.end() ; meshAttributeIterator++) {
            //(*it).first;             // the key value (of type Key)
            //(*it).second;            // the mapped value (of type T)
            shared_ptr <GLTF::GLTFMeshAttribute> meshAttribute = (*meshAttributeIterator).second;
            
            meshAttribute->apply(__InvertV, NULL);
        }
        
        if (cvtMesh->getPrimitives().size() > 0) {
            //After this point cvtMesh should be referenced anymore and will be deallocated
            shared_ptr <GLTF::GLTFMesh> unifiedMesh = createUnifiedIndexesMeshFromMesh(cvtMesh.get(), allPrimitiveIndicesVectors);
            if  (createMeshesWithMaximumIndicesCountFromMeshIfNeeded(unifiedMesh.get(), 65535, meshes) == false) {
                meshes.push_back(unifiedMesh);
            }
        }
    }

    
}
