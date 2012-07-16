/*
  OpenMW - The completely unofficial reimplementation of Morrowind
  Copyright (C) 2008-2010  Nicolay Korslund
  Email: < korslund@gmail.com >
  WWW: http://openmw.sourceforge.net/

  This file (ogre_nif_loader.cpp) is part of the OpenMW package.

  OpenMW is distributed as free software: you can redistribute it
  and/or modify it under the terms of the GNU General Public License
  version 3, as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  version 3 along with this program. If not, see
  http://www.gnu.org/licenses/ .

 */

//loadResource->handleNode->handleNiTriShape->createSubMesh

#include "ogre_nif_loader.hpp"

#include <OgreMaterialManager.h>
#include <OgreMeshManager.h>
#include <OgreHardwareBufferManager.h>
#include <OgreSkeletonManager.h>
#include <OgreTechnique.h>
#include <OgreSubMesh.h>
#include <OgreRoot.h>

#include <components/settings/settings.hpp>
#include <components/nifoverrides/nifoverrides.hpp>

typedef unsigned char ubyte;

using namespace std;
using namespace Nif;
using namespace NifOgre;


// Helper class that computes the bounding box and of a mesh
class BoundsFinder
{
    struct MaxMinFinder
    {
        float max, min;

        MaxMinFinder()
        {
            min = numeric_limits<float>::infinity();
            max = -min;
        }

        void add(float f)
        {
            if (f > max) max = f;
            if (f < min) min = f;
        }

        // Return Max(max**2, min**2)
        float getMaxSquared()
        {
            float m1 = max*max;
            float m2 = min*min;
            if (m1 >= m2) return m1;
            return m2;
        }
    };

    MaxMinFinder X, Y, Z;

public:
    // Add 'verts' vertices to the calculation. The 'data' pointer is
    // expected to point to 3*verts floats representing x,y,z for each
    // point.
    void add(float *data, int verts)
    {
        for (int i=0;i<verts;i++)
        {
            X.add(*(data++));
            Y.add(*(data++));
            Z.add(*(data++));
        }
    }

    // True if this structure has valid values
    bool isValid()
    {
        return
            minX() <= maxX() &&
            minY() <= maxY() &&
            minZ() <= maxZ();
    }

    // Compute radius
    float getRadius()
    {
        assert(isValid());

        // The radius is computed from the origin, not from the geometric
        // center of the mesh.
        return sqrt(X.getMaxSquared() + Y.getMaxSquared() + Z.getMaxSquared());
    }

    float minX() {
        return X.min;
    }
    float maxX() {
        return X.max;
    }
    float minY() {
        return Y.min;
    }
    float maxY() {
        return Y.max;
    }
    float minZ() {
        return Z.min;
    }
    float maxZ() {
        return Z.max;
    }
};


struct NIFSkeletonLoader : public Ogre::ManualResourceLoader {

static void warn(const std::string &msg)
{
    std::cerr << "NIFSkeletonLoader: Warn: " << msg << std::endl;
}

static void fail(const std::string &msg)
{
    std::cerr << "NIFSkeletonLoader: Fail: "<< msg << std::endl;
    abort();
}


void buildBones(Ogre::Skeleton *skel, Nif::NiNode *node, Ogre::Bone *parent=NULL)
{
    Ogre::Bone *bone;
    if(!skel->hasBone(node->name))
        bone = skel->createBone(node->name);
    else
        bone = skel->createBone();
    if(parent) parent->addChild(bone);

    bone->setOrientation(node->trafo.rotation);
    bone->setPosition(node->trafo.pos);
    bone->setScale(Ogre::Vector3(node->trafo.scale));
    bone->setBindingPose();
    bone->setInitialState();

    const Nif::NodeList &children = node->children;
    for(size_t i = 0;i < children.length();i++)
    {
        Nif::NiNode *next;
        if(!children[i].empty() && (next=dynamic_cast<Nif::NiNode*>(children[i].getPtr())))
            buildBones(skel, next, bone);
    }
}

void loadResource(Ogre::Resource *resource)
{
    Ogre::Skeleton *skel = dynamic_cast<Ogre::Skeleton*>(resource);
    OgreAssert(skel, "Attempting to load a skeleton into a non-skeleton resource!");

    Nif::NIFFile nif(skel->getName());
    Nif::NiNode *node = dynamic_cast<Nif::NiNode*>(nif.getRecord(0));
    buildBones(skel, node);
}

static bool createSkeleton(const std::string &name, const std::string &group, Nif::Node *node, Ogre::SkeletonPtr *skel)
{
    if(node->boneTrafo != NULL)
    {
        Ogre::SkeletonManager &skelMgr = Ogre::SkeletonManager::getSingleton();

        Ogre::SkeletonPtr tmp = skelMgr.getByName(name);
        if(tmp.isNull())
        {
            static NIFSkeletonLoader loader;
            tmp = skelMgr.create(name, group, true, &loader);
        }

        if(skel) *skel = tmp;
        return true;
    }

    Nif::NiNode *ninode = dynamic_cast<Nif::NiNode*>(node);
    if(ninode)
    {
        Nif::NodeList &children = ninode->children;
        for(size_t i = 0;i < children.length();i++)
        {
            if(!children[i].empty())
            {
                if(createSkeleton(name, group, children[i].getPtr(), skel))
                    return true;
            }
        }
    }
    return false;
}

};


// Conversion of blend / test mode from NIF -> OGRE.
// Not in use yet, so let's comment it out.
/*
static SceneBlendFactor getBlendFactor(int mode)
{
  switch(mode)
    {
    case 0: return SBF_ONE;
    case 1: return SBF_ZERO;
    case 2: return SBF_SOURCE_COLOUR;
    case 3: return SBF_ONE_MINUS_SOURCE_COLOUR;
    case 4: return SBF_DEST_COLOUR;
    case 5: return SBF_ONE_MINUS_DEST_COLOUR;
    case 6: return SBF_SOURCE_ALPHA;
    case 7: return SBF_ONE_MINUS_SOURCE_ALPHA;
    case 8: return SBF_DEST_ALPHA;
    case 9: return SBF_ONE_MINUS_DEST_ALPHA;
      // [Comment from Chris Robinson:] Can't handle this mode? :/
      // case 10: return SBF_SOURCE_ALPHA_SATURATE;
    default:
      return SBF_SOURCE_ALPHA;
    }
}


// This is also unused
static CompareFunction getTestMode(int mode)
{
  switch(mode)
    {
    case 0: return CMPF_ALWAYS_PASS;
    case 1: return CMPF_LESS;
    case 2: return CMPF_EQUAL;
    case 3: return CMPF_LESS_EQUAL;
    case 4: return CMPF_GREATER;
    case 5: return CMPF_NOT_EQUAL;
    case 6: return CMPF_GREATER_EQUAL;
    case 7: return CMPF_ALWAYS_FAIL;
    default:
      return CMPF_ALWAYS_PASS;
    }
}
*/


class NIFMaterialLoader {

static std::multimap<std::string,std::string> MaterialMap;

static void warn(const std::string &msg)
{
    std::cerr << "NIFMeshLoader: Warn: " << msg << std::endl;
}

static void fail(const std::string &msg)
{
    std::cerr << "NIFMeshLoader: Fail: "<< msg << std::endl;
    abort();
}

public:
static Ogre::String getMaterial(const NiTriShape *shape, const Ogre::String &name, const Ogre::String &group)
{
    Ogre::MaterialManager &matMgr = Ogre::MaterialManager::getSingleton();
    Ogre::MaterialPtr material = matMgr.getByName(name);
    if(!material.isNull())
        return name;

    Ogre::Vector3 ambient(1.0f);
    Ogre::Vector3 diffuse(1.0f);
    Ogre::Vector3 specular(0.0f);
    Ogre::Vector3 emissive(0.0f);
    float glossiness = 0.0f;
    float alpha = 1.0f;
    int alphaFlags = -1;
    ubyte alphaTest = 0;
    Ogre::String texName;

    // These are set below if present
    const NiTexturingProperty *t = NULL;
    const NiMaterialProperty *m = NULL;
    const NiAlphaProperty *a = NULL;

    // Scan the property list for material information
    const PropertyList &list = shape->props;
    for (size_t i = 0;i < list.length();i++)
    {
        // Entries may be empty
        if (list[i].empty()) continue;

        const Property *pr = list[i].getPtr();
        if (pr->recType == RC_NiTexturingProperty)
            t = static_cast<const NiTexturingProperty*>(pr);
        else if (pr->recType == RC_NiMaterialProperty)
            m = static_cast<const NiMaterialProperty*>(pr);
        else if (pr->recType == RC_NiAlphaProperty)
            a = static_cast<const NiAlphaProperty*>(pr);
        else
            warn("Skipped property type: "+pr->recName);
    }

    // Texture
    if (t && t->textures[0].inUse)
    {
        NiSourceTexture *st = t->textures[0].texture.getPtr();
        if (st->external)
        {
            /* Bethesda at some at some point converted all their BSA
             * textures from tga to dds for increased load speed, but all
             * texture file name references were kept as .tga.
             */
            texName = "textures\\" + st->filename;
            if(!Ogre::ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(texName))
            {
                Ogre::String::size_type pos = texName.rfind('.');
                texName.replace(pos, texName.length(), ".dds");
            }
        }
        else warn("Found internal texture, ignoring.");
    }

    // Alpha modifiers
    if (a)
    {
        alphaFlags = a->flags;
        alphaTest = a->data.threshold;
    }

    // Material
    if(m)
    {
        ambient = m->data.ambient;
        diffuse = m->data.diffuse;
        specular = m->data.specular;
        emissive = m->data.emissive;
        glossiness = m->data.glossiness;
        alpha = m->data.alpha;
    }

    Ogre::String matname = name;
    if (m || !texName.empty())
    {
        // If we're here, then this mesh has a material. Thus we
        // need to calculate a snappy material name. It should
        // contain the mesh name (mesh->getName()) but also has to
        // be unique. One mesh may use many materials.
        std::multimap<std::string,std::string>::iterator itr = MaterialMap.find(texName);
        std::multimap<std::string,std::string>::iterator lastElement;
        lastElement = MaterialMap.upper_bound(texName);
        if (itr != MaterialMap.end())
        {
            for ( ; itr != lastElement; ++itr)
            {
                //std::cout << "OK!";
                //MaterialPtr mat = MaterialManager::getSingleton().getByName(itr->second,recourceGroup);
                return itr->second;
                //if( mat->getA
            }
        }
    }

    // No existing material like this. Create a new one.
    material = matMgr.create(matname, group, true);

    // This assigns the texture to this material. If the texture name is
    // a file name, and this file exists (in a resource directory), it
    // will automatically be loaded when needed. If not (such as for
    // internal NIF textures that we might support later), we should
    // already have inserted a manual loader for the texture.
    if (!texName.empty())
    {
        Ogre::Pass *pass = material->getTechnique(0)->getPass(0);
        /*TextureUnitState *txt =*/
        pass->createTextureUnitState(texName);

        pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);

        // As of yet UNTESTED code from Chris:
        /*pass->setTextureFiltering(Ogre::TFO_ANISOTROPIC);
        pass->setDepthFunction(Ogre::CMPF_LESS_EQUAL);
        pass->setDepthCheckEnabled(true);

        // Add transparency if NiAlphaProperty was present
        if (alphaFlags != -1)
        {
            std::cout << "Alpha flags set!" << endl;
            if ((alphaFlags&1))
            {
                pass->setDepthWriteEnabled(false);
                pass->setSceneBlending(getBlendFactor((alphaFlags>>1)&0xf),
                                       getBlendFactor((alphaFlags>>5)&0xf));
            }
            else
                pass->setDepthWriteEnabled(true);

            if ((alphaFlags>>9)&1)
                pass->setAlphaRejectSettings(getTestMode((alphaFlags>>10)&0x7),
                                             alphaTest);

            pass->setTransparentSortingEnabled(!((alphaFlags>>13)&1));
        }
        else
            pass->setDepthWriteEnabled(true); */


        // Add transparency if NiAlphaProperty was present
        if (alphaFlags != -1)
        {
            // The 237 alpha flags are by far the most common. Check
            // NiAlphaProperty in nif/property.h if you need to decode
            // other values. 237 basically means normal transparencly.
            if (alphaFlags == 237)
            {
                NifOverrides::TransparencyResult result = NifOverrides::Overrides::getTransparencyOverride(texName);
                if (result.first)
                {
                    pass->setAlphaRejectFunction(Ogre::CMPF_GREATER_EQUAL);
                    pass->setAlphaRejectValue(result.second);
                }
                else
                {
                    // Enable transparency
                    pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);

                    //pass->setDepthCheckEnabled(false);
                    pass->setDepthWriteEnabled(false);
                    //std::cout << "alpha 237; material: " << name << " texName: " << texName << std::endl;
                }
            }
            else
                warn("Unhandled alpha setting for texture " + texName);
        }
        else
        {
            material->getTechnique(0)->setShadowCasterMaterial("depth_shadow_caster_noalpha");
        }
    }

    if (Settings::Manager::getBool("enabled", "Shadows"))
    {
        bool split = Settings::Manager::getBool("split", "Shadows");
        const int numsplits = 3;
        for (int i = 0; i < (split ? numsplits : 1); ++i)
        {
            Ogre::TextureUnitState* tu = material->getTechnique(0)->getPass(0)->createTextureUnitState();
            tu->setName("shadowMap" + Ogre::StringConverter::toString(i));
            tu->setContentType(Ogre::TextureUnitState::CONTENT_SHADOW);
            tu->setTextureAddressingMode(Ogre::TextureUnitState::TAM_BORDER);
            tu->setTextureBorderColour(Ogre::ColourValue::White);
        }
    }

    if (Settings::Manager::getBool("shaders", "Objects"))
    {
        material->getTechnique(0)->getPass(0)->setVertexProgram("main_vp");
        material->getTechnique(0)->getPass(0)->setFragmentProgram("main_fp");

        material->getTechnique(0)->getPass(0)->setFog(true); // force-disable fixed function fog, it is calculated in shader
    }

    // Create a fallback technique without shadows and without mrt
    Ogre::Technique* tech2 = material->createTechnique();
    tech2->setSchemeName("Fallback");
    Ogre::Pass* pass2 = tech2->createPass();
    pass2->createTextureUnitState(texName);
    pass2->setVertexColourTracking(Ogre::TVC_DIFFUSE);
    if (Settings::Manager::getBool("shaders", "Objects"))
    {
        pass2->setVertexProgram("main_fallback_vp");
        pass2->setFragmentProgram("main_fallback_fp");
        pass2->setFog(true); // force-disable fixed function fog, it is calculated in shader
    }

    // Add material bells and whistles
    material->setAmbient(ambient[0], ambient[1], ambient[2]);
    material->setDiffuse(diffuse[0], diffuse[1], diffuse[2], alpha);
    material->setSpecular(specular[0], specular[1], specular[2], alpha);
    material->setSelfIllumination(emissive[0], emissive[1], emissive[2]);
    material->setShininess(glossiness);

    MaterialMap.insert(std::make_pair(texName, matname));
    return matname;
}

};
std::multimap<std::string,std::string> NIFMaterialLoader::MaterialMap;


class NIFMeshLoader : Ogre::ManualResourceLoader
{
    std::string mName;
    std::string mGroup;
    std::string mShapeName;
    std::string mMaterialName;
    bool mHasSkel;

    void warn(const std::string &msg)
    {
        std::cerr << "NIFMeshLoader: Warn: " << msg << std::endl;
    }

    void fail(const std::string &msg)
    {
        std::cerr << "NIFMeshLoader: Fail: "<< msg << std::endl;
        abort();
    }


    // Convert NiTriShape to Ogre::SubMesh
    void handleNiTriShape(Ogre::Mesh *mesh, Nif::NiTriShape *shape)
    {
        const Nif::NiTriShapeData *data = shape->data.getPtr();
        const Nif::NiSkinInstance *skin = (shape->skin.empty() ? NULL : shape->skin.getPtr());
        std::vector<Ogre::Vector3> srcVerts = data->vertices;
        std::vector<Ogre::Vector3> srcNorms = data->normals;
        if(skin != NULL)
        {
            // Only set a skeleton when skinning. Unskinned meshes with a skeleton will be
            // explicitly attached later.
            mesh->setSkeletonName(mName);

            // Convert vertices and normals to bone space from bind position. It would be
            // better to transform the bones into bind position, but there doesn't seem to
            // be a reliable way to do that.
            std::vector<Ogre::Vector3> newVerts(srcVerts.size(), Ogre::Vector3(0.0f));
            std::vector<Ogre::Vector3> newNorms(srcNorms.size(), Ogre::Vector3(1.0f));

            const Nif::NiSkinData *data = skin->data.getPtr();
            const Nif::NodeList &bones = skin->bones;
            for(size_t b = 0;b < bones.length();b++)
            {
                Ogre::Matrix4 mat(Ogre::Matrix4::IDENTITY);
                mat.makeTransform(data->bones[b].trafo.trans, Ogre::Vector3(data->bones[b].trafo.scale),
                                  Ogre::Quaternion(data->bones[b].trafo.rotation));
                mat = bones[b]->getWorldTransform() * mat;

                const std::vector<Nif::NiSkinData::VertWeight> &weights = data->bones[b].weights;
                for(size_t i = 0;i < weights.size();i++)
                {
                    size_t index = weights[i].vertex;
                    float weight = weights[i].weight;

                    newVerts.at(index) += (mat*srcVerts[index]) * weight;
                    if(newNorms.size() > index)
                    {
                        Ogre::Vector4 vec4(srcNorms[index][0], srcNorms[index][1], srcNorms[index][2], 0.0f);
                        vec4 = mat*vec4 * weight;
                        newNorms[index] += Ogre::Vector3(&vec4[0]);
                    }
                }
            }

            srcVerts = newVerts;
            srcNorms = newNorms;
        }
        else if(!mHasSkel)
        {
            // No skinning and no skeleton, so just transform the vertices and
            // normals into position.
            Ogre::Matrix4 mat4 = shape->getWorldTransform();
            for(size_t i = 0;i < srcVerts.size();i++)
            {
                Ogre::Vector4 vec4(srcVerts[i].x, srcVerts[i].y, srcVerts[i].z, 1.0f);
                vec4 = mat4*vec4;
                srcVerts[i] = Ogre::Vector3(&vec4[0]);
            }
            for(size_t i = 0;i < srcNorms.size();i++)
            {
                Ogre::Vector4 vec4(srcNorms[i].x, srcNorms[i].y, srcNorms[i].z, 0.0f);
                vec4 = mat4*vec4;
                srcNorms[i] = Ogre::Vector3(&vec4[0]);
            }
        }

        // Set the bounding box first
        BoundsFinder bounds;
        bounds.add(&srcVerts[0][0], srcVerts.size());
        // No idea why this offset is needed. It works fine without it if the
        // vertices weren't transformed first, but otherwise it fails later on
        // when the object is being inserted into the scene.
        mesh->_setBounds(Ogre::AxisAlignedBox(bounds.minX()-0.5f, bounds.minY()-0.5f, bounds.minZ()-0.5f,
                                              bounds.maxX()+0.5f, bounds.maxY()+0.5f, bounds.maxZ()+0.5f));
        mesh->_setBoundingSphereRadius(bounds.getRadius());

        // This function is just one long stream of Ogre-barf, but it works
        // great.
        Ogre::HardwareBufferManager *hwBufMgr = Ogre::HardwareBufferManager::getSingletonPtr();
        Ogre::HardwareVertexBufferSharedPtr vbuf;
        Ogre::HardwareIndexBufferSharedPtr ibuf;
        Ogre::VertexBufferBinding *bind;
        Ogre::VertexDeclaration *decl;
        int nextBuf = 0;

        Ogre::SubMesh *sub = mesh->createSubMesh(shape->name);

        // Add vertices
        sub->useSharedVertices = false;
        sub->vertexData = new Ogre::VertexData();
        sub->vertexData->vertexStart = 0;
        sub->vertexData->vertexCount = srcVerts.size();

        decl = sub->vertexData->vertexDeclaration;
        bind = sub->vertexData->vertexBufferBinding;
        if(srcVerts.size())
        {
            vbuf = hwBufMgr->createVertexBuffer(Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3),
                                                srcVerts.size(), Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY,
                                                true);
            vbuf->writeData(0, vbuf->getSizeInBytes(), &srcVerts[0][0], true);

            decl->addElement(nextBuf, 0, Ogre::VET_FLOAT3, Ogre::VES_POSITION);
            bind->setBinding(nextBuf++, vbuf);
        }

        // Vertex normals
        if(srcNorms.size())
        {
            vbuf = hwBufMgr->createVertexBuffer(Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3),
                                                srcNorms.size(), Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY,
                                                true);
            vbuf->writeData(0, vbuf->getSizeInBytes(), &srcNorms[0][0], true);

            decl->addElement(nextBuf, 0, Ogre::VET_FLOAT3, Ogre::VES_NORMAL);
            bind->setBinding(nextBuf++, vbuf);
        }

        // Vertex colors
        const std::vector<Ogre::Vector4> &colors = data->colors;
        if(colors.size())
        {
            Ogre::RenderSystem* rs = Ogre::Root::getSingleton().getRenderSystem();
            std::vector<Ogre::RGBA> colorsRGB(colors.size());
            for(size_t i = 0;i < colorsRGB.size();i++)
            {
                Ogre::ColourValue clr(colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
                rs->convertColourValue(clr, &colorsRGB[i]);
            }
            vbuf = hwBufMgr->createVertexBuffer(Ogre::VertexElement::getTypeSize(Ogre::VET_COLOUR),
                                                colorsRGB.size(), Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
                                                true);
            vbuf->writeData(0, vbuf->getSizeInBytes(), &colorsRGB[0], true);
            decl->addElement(nextBuf, 0, Ogre::VET_COLOUR, Ogre::VES_DIFFUSE);
            bind->setBinding(nextBuf++, vbuf);
        }

        // Texture UV coordinates
        size_t numUVs = data->uvlist.size();
        if(numUVs)
        {
            size_t elemSize = Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT2);
            vbuf = hwBufMgr->createVertexBuffer(elemSize, srcVerts.size()*numUVs,
                                                Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY, true);
            for(size_t i = 0;i < numUVs;i++)
            {
                const std::vector<Ogre::Vector2> &uvlist = data->uvlist[i];
                vbuf->writeData(i*srcVerts.size()*elemSize, elemSize*srcVerts.size(), &uvlist[0], true);
                decl->addElement(nextBuf, i*srcVerts.size()*elemSize, Ogre::VET_FLOAT2,
                                 Ogre::VES_TEXTURE_COORDINATES, i);
            }
            bind->setBinding(nextBuf++, vbuf);
        }

        // Triangle faces
        const std::vector<short> &srcIdx = data->triangles;
        if(srcIdx.size())
        {
            ibuf = hwBufMgr->createIndexBuffer(Ogre::HardwareIndexBuffer::IT_16BIT, srcIdx.size(),
                                               Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY);
            ibuf->writeData(0, ibuf->getSizeInBytes(), &srcIdx[0], true);
            sub->indexData->indexBuffer = ibuf;
            sub->indexData->indexCount = srcIdx.size();
            sub->indexData->indexStart = 0;
        }

        // Assign bone weights for this TriShape
        if(skin != NULL)
        {
            // Get the skeleton resource, so weights can be applied
            Ogre::SkeletonManager *skelMgr = Ogre::SkeletonManager::getSingletonPtr();
            Ogre::SkeletonPtr skel = skelMgr->getByName(mesh->getSkeletonName());
            skel->touch();

            const Nif::NiSkinData *data = skin->data.getPtr();
            const Nif::NodeList &bones = skin->bones;
            for(size_t i = 0;i < bones.length();i++)
            {
                Ogre::VertexBoneAssignment boneInf;
                boneInf.boneIndex = skel->getBone(bones[i]->name)->getHandle();

                const std::vector<Nif::NiSkinData::VertWeight> &weights = data->bones[i].weights;
                for(size_t j = 0;j < weights.size();j++)
                {
                    boneInf.vertexIndex = weights[j].vertex;
                    boneInf.weight = weights[j].weight;
                    sub->addBoneAssignment(boneInf);
                }
            }
        }

        if(mMaterialName.length() > 0)
            sub->setMaterialName(mMaterialName);
    }

    bool findTriShape(Ogre::Mesh *mesh, Nif::Node *node)
    {
        if(node->recType == Nif::RC_NiTriShape && mShapeName == node->name)
        {
            handleNiTriShape(mesh, dynamic_cast<Nif::NiTriShape*>(node));
            return true;
        }

        Nif::NiNode *ninode = dynamic_cast<Nif::NiNode*>(node);
        if(ninode)
        {
            Nif::NodeList &children = ninode->children;
            for(size_t i = 0;i < children.length();i++)
            {
                if(!children[i].empty())
                {
                    if(findTriShape(mesh, children[i].getPtr()))
                        return true;
                }
            }
        }
        return false;
    }


    typedef std::map<std::string,NIFMeshLoader,ciLessBoost> LoaderMap;
    static LoaderMap sLoaders;

public:
    NIFMeshLoader()
      : mHasSkel(false)
    { }
    NIFMeshLoader(const std::string &name, const std::string &group, bool hasSkel)
      : mName(name), mGroup(group), mHasSkel(hasSkel)
    { }

    virtual void loadResource(Ogre::Resource *resource)
    {
        Ogre::Mesh *mesh = dynamic_cast<Ogre::Mesh*>(resource);
        assert(mesh && "Attempting to load a mesh into a non-mesh resource!");

        if(!mShapeName.length())
        {
            if(mHasSkel)
                mesh->setSkeletonName(mName);
            return;
        }

        Nif::NIFFile nif(mName);
        Nif::Node *node = dynamic_cast<Nif::Node*>(nif.getRecord(0));
        findTriShape(mesh, node);
    }

    void createMeshes(const Nif::Node *node, MeshPairList &meshes, int flags=0)
    {
        flags |= node->flags;

        Nif::ExtraPtr e = node->extra;
        while(!e.empty())
        {
            Nif::NiStringExtraData *sd = dynamic_cast<Nif::NiStringExtraData*>(e.getPtr());
            if(sd != NULL)
            {
                // String markers may contain important information
                // affecting the entire subtree of this obj
                if(sd->string == "MRK")
                {
                    // Marker objects. These are only visible in the
                    // editor.
                    flags |= 0x01;
                }
            }
            else
                warn("Unhandled extra data type "+e->recType);
            e = e->extra;
        }

        if(node->recType == Nif::RC_NiTriShape)
        {
            const NiTriShape *shape = dynamic_cast<const NiTriShape*>(node);

            Ogre::MeshManager &meshMgr = Ogre::MeshManager::getSingleton();
            std::string fullname = mName+"@"+shape->name;

            Ogre::MeshPtr mesh = meshMgr.getByName(fullname);
            if(mesh.isNull())
            {
                NIFMeshLoader *loader = &sLoaders[fullname];
                *loader = *this;
                if(!(flags&0x01)) // Not hidden
                {
                    loader->mShapeName = shape->name;
                    loader->mMaterialName = NIFMaterialLoader::getMaterial(shape, fullname, mGroup);
                }

                mesh = meshMgr.createManual(fullname, mGroup, loader);
            }

            meshes.push_back(std::make_pair(mesh, (shape->parent ? shape->parent->name : std::string())));
        }
        else if(node->recType != Nif::RC_NiNode && node->recType != Nif::RC_RootCollisionNode &&
                node->recType != Nif::RC_NiRotatingParticles)
            warn("Unhandled mesh node type: "+node->recName);

        const Nif::NiNode *ninode = dynamic_cast<const Nif::NiNode*>(node);
        if(ninode)
        {
            const Nif::NodeList &children = ninode->children;
            for(size_t i = 0;i < children.length();i++)
            {
                if(!children[i].empty())
                    createMeshes(children[i].getPtr(), meshes, flags);
            }
        }
    }
};
NIFMeshLoader::LoaderMap NIFMeshLoader::sLoaders;


MeshPairList NIFLoader::load(const std::string &name, Ogre::SkeletonPtr *skel, const std::string &group)
{
    MeshPairList meshes;

    if(skel != NULL)
        skel->setNull();

    Nif::NIFFile nif(name);
    if (nif.numRecords() < 1)
    {
        nif.warn("Found no records in NIF.");
        return meshes;
    }

    // The first record is assumed to be the root node
    Nif::Record *r = nif.getRecord(0);
    assert(r != NULL);

    Nif::Node *node = dynamic_cast<Nif::Node*>(r);
    if(node == NULL)
    {
        nif.warn("First record in file was not a node, but a "+
                 r->recName+". Skipping file.");
        return meshes;
    }

    bool hasSkel = NIFSkeletonLoader::createSkeleton(name, group, node, skel);

    NIFMeshLoader meshldr(name, group, hasSkel);
    meshldr.createMeshes(node, meshes);

    return meshes;
}


/* More code currently not in use, from the old D source. This was
   used in the first attempt at loading NIF meshes, where each submesh
   in the file was given a separate bone in a skeleton. Unfortunately
   the OGRE skeletons can't hold more than 256 bones, and some NIFs go
   way beyond that. The code might be of use if we implement animated
   submeshes like this (the part of the NIF that is animated is
   usually much less than the entire file, but the method might still
   not be water tight.)

// Insert a raw RGBA image into the texture system.
extern "C" void ogre_insertTexture(char* name, uint32_t width, uint32_t height, void *data)
{
  TexturePtr texture = TextureManager::getSingleton().createManual(
      name,         // name
      "General",    // group
      TEX_TYPE_2D,      // type
      width, height,    // width & height
      0,                // number of mipmaps
      PF_BYTE_RGBA,     // pixel format
      TU_DEFAULT);      // usage; should be TU_DYNAMIC_WRITE_ONLY_DISCARDABLE for
                        // textures updated very often (e.g. each frame)

  // Get the pixel buffer
  HardwarePixelBufferSharedPtr pixelBuffer = texture->getBuffer();

  // Lock the pixel buffer and get a pixel box
  pixelBuffer->lock(HardwareBuffer::HBL_NORMAL); // for best performance use HBL_DISCARD!
  const PixelBox& pixelBox = pixelBuffer->getCurrentLock();

  void *dest = pixelBox.data;

  // Copy the data
  memcpy(dest, data, width*height*4);

  // Unlock the pixel buffer
  pixelBuffer->unlock();
}


*/
