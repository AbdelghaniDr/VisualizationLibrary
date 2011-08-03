/**************************************************************************************/
/*                                                                                    */
/*  Visualization Library                                                             */
/*  http://www.visualizationlibrary.org                                               */
/*                                                                                    */
/*  Copyright (c) 2005-2010, Michele Bosi                                             */
/*  All rights reserved.                                                              */
/*                                                                                    */
/*  Redistribution and use in source and binary forms, with or without modification,  */
/*  are permitted provided that the following conditions are met:                     */
/*                                                                                    */
/*  - Redistributions of source code must retain the above copyright notice, this     */
/*  list of conditions and the following disclaimer.                                  */
/*                                                                                    */
/*  - Redistributions in binary form must reproduce the above copyright notice, this  */
/*  list of conditions and the following disclaimer in the documentation and/or       */
/*  other materials provided with the distribution.                                   */
/*                                                                                    */
/*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND   */
/*  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED     */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE            */
/*  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR  */
/*  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES    */
/*  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;      */
/*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON    */
/*  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT           */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS     */
/*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                      */
/*                                                                                    */
/**************************************************************************************/

#include <vlGraphics/plugins/vlCOLLADA.hpp>
#include <vlCore/FileSystem.hpp>
#include <vlGraphics/Geometry.hpp>
#include <vlGraphics/Actor.hpp>
#include <vlGraphics/Effect.hpp>
#include <vlGraphics/Light.hpp>
#include <vlCore/Time.hpp>
#include <vlCore/GLSLmath.hpp>
#include <set>
#include <dae.h>
#include <dae/domAny.h>
#include <dom.h>
#include <dom/domCOLLADA.h>
#include <dom/domProfile_COMMON.h>

using namespace vl;

// --- mic fixme: remove debug code ---
bool debug_Wireframe = false;
// ---
int debug_IndentLevel = 0;
// ---
mat4 debug_WorldMatrix;
// ---
std::vector< ref<Actor> > debug_Actors;
// ---
float debug_GeometryTime = 0;
// ---
Time debug_Timer;
// ---
void debug_PrintIndent(const String& str)
{
  for(int i=0; i<debug_IndentLevel; ++i)
    Log::print("  ");
  Log::print(str);
}
// ---
bool debug_IsMatrixSane(const mat4& m)
{
  for(int i=0; i<16; i++)
    if( m.ptr()[i] != m.ptr()[i] )
      return false;
  return true;
}
// ---
void debug_PrintMatrix(const fmat4& m)
{
  debug_PrintIndent("<matrix>\n");
  debug_PrintIndent( Say("%n %n %n %n\n") << m.e(0, 0) << m.e(0, 1) << m.e(0, 2) << m.e(0, 3) );
  debug_PrintIndent( Say("%n %n %n %n\n") << m.e(1, 0) << m.e(1, 1) << m.e(1, 2) << m.e(1, 3) );
  debug_PrintIndent( Say("%n %n %n %n\n") << m.e(2, 0) << m.e(2, 1) << m.e(2, 2) << m.e(2, 3) );
  debug_PrintIndent( Say("%n %n %n %n\n") << m.e(3, 0) << m.e(3, 1) << m.e(3, 2) << m.e(3, 3) );
}
// ---
int debug_ExtractGeometry = false;
// ---
void debug_PrintMatrices(const Transform* tr)
{
  debug_IndentLevel++;

  debug_PrintMatrix( tr->worldMatrix() );
  for(size_t i=0; i<tr->childrenCount(); ++i)
  {
    debug_PrintMatrices( tr->children()[i].get() );
  }

  debug_IndentLevel--;
}
// ---
void debug_PrintGometry(Actor* actor)
{
  Geometry* geom = actor->lod(0)->as<Geometry>();

  debug_PrintIndent( "[VERTICES]\n" );
  for(size_t i=0; i<geom->vertexArray()->size(); ++i)
  {
    vec3 v = geom->vertexArray()->getAsVec3(i);
    debug_PrintIndent( Say("    %n %n %n \n") << v.x() << v.y() << v.z() );
  }
  debug_PrintIndent( "[/VERTICES]\n" );

  debug_PrintIndent( "[NORMALS]\n" );
  for(size_t i=0; i<geom->normalArray()->size(); ++i)
  {
    vec3 v = geom->normalArray()->getAsVec3(i);
    debug_PrintIndent( Say("    %n %n %n \n") << v.x() << v.y() << v.z() );
  }
  debug_PrintIndent( "[/NORMALS]\n" );

  debug_PrintIndent( "[TRIANGLES]\n" );
  for(int i=0; i<geom->drawCalls()->size(); ++i)
  {
    DrawCall* dc = geom->drawCalls()->at(i);
    TriangleIterator it = dc->triangleIterator();
    for( ; it.hasNext(); it.next() )
      debug_PrintIndent( Say("  %n %n %n \n") << it.a() << it.b() << it.c() );
  }
  debug_PrintIndent( "[/TRIANGLES]\n" );
}
// ---

namespace vl
{
  namespace Dae
  {
    //-----------------------------------------------------------------------------
    const char* NO_MATERIAL_SPECIFIED = "<NO_MATERIAL_SPECIFIED>";
    //-----------------------------------------------------------------------------
    typedef enum { PT_UNKNOWN, PT_LINES, PT_LINE_STRIP, PT_POLYGONS, PT_POLYLIST, PT_TRIANGLES, PT_TRIFANS, PT_TRISTRIPS } EPrimitiveType;
    //-----------------------------------------------------------------------------
    typedef enum
    {
      IS_UNKNOWN,
      IS_BINORMAL,
      IS_COLOR,
      IS_CONTINUITY,
      IS_IMAGE,
      IS_INPUT,
      IS_IN_TANGENT,
      IS_INTERPOLATION,
      IS_INV_BIND_MATRIX,
      IS_JOINT,
      IS_LINEAR_STEPS,
      IS_MORPHS_TARGET,
      IS_MORPH_WEIGHT,
      IS_NORMAL,
      IS_OUTPUT,
      IS_OUT_TANGENT,
      IS_POSITION,
      IS_TANGENT,
      IS_TEXBINORMAL,
      IS_TEXCOORD,
      IS_TEXTANGENT,
      IS_UV,
      IS_VERTEX,
      IS_WEIGHT
    } EInputSemantic;
    //-----------------------------------------------------------------------------
    struct 
    {
      EInputSemantic mSemantic;
      const char* mSemanticString;
    } SemanticTable[] = 
      {
        { IS_UNKNOWN,         "UNKNOWN"         },
        { IS_BINORMAL,        "BINORMAL"        },
        { IS_COLOR,           "COLOR"           },
        { IS_CONTINUITY,      "CONTINUITY"      },
        { IS_IMAGE,           "IMAGE"           },
        { IS_INPUT,           "INPUT"           },
        { IS_IN_TANGENT,      "IN_TANGENT"      },
        { IS_INTERPOLATION,   "INTERPOLATION"   },
        { IS_INV_BIND_MATRIX, "INV_BIND_MATRIX" },
        { IS_JOINT,           "JOINT"           },
        { IS_LINEAR_STEPS,    "LINEAR_STEPS"    },
        { IS_MORPHS_TARGET,   "MORPHS_TARGET"   },
        { IS_MORPH_WEIGHT,    "MORPH_WEIGHT"    },
        { IS_NORMAL,          "NORMAL"          },
        { IS_OUTPUT,          "OUTPUT"          },
        { IS_OUT_TANGENT,     "OUT_TANGENT"     },
        { IS_POSITION,        "POSITION"        },
        { IS_TANGENT,         "TANGENT"         },
        { IS_TEXBINORMAL,     "TEXBINORMAL"     },
        { IS_TEXCOORD,        "TEXCOORD"        },
        { IS_TEXTANGENT,      "TEXTANGENT"      },
        { IS_UV,              "UV"              },
        { IS_VERTEX,          "VERTEX"          },
        { IS_WEIGHT,          "WEIGHT"          },
        { IS_UNKNOWN,          NULL             }
      };
  }
}
//-----------------------------------------------------------------------------
struct DaeVert
{
  static const int MAX_ATTRIBS = 8;

  DaeVert()
  {
    memset(mAttribIndex, 0xFF, sizeof(mAttribIndex));
    mIndex = (size_t)-1;
  }

  bool operator<(const DaeVert& other) const
  {
    for(int i=0; i<MAX_ATTRIBS; ++i)
    {
      if (mAttribIndex[i] != other.mAttribIndex[i])
        return mAttribIndex[i] < other.mAttribIndex[i];
    }
    return false;
  }

  size_t mAttribIndex[MAX_ATTRIBS];
  size_t mIndex;
};
//-----------------------------------------------------------------------------
class DaeSource: public Object
{
public:
  DaeSource()
  {
    mFloatSource = NULL;
    mIntSource   = NULL;
    mBoolSource  = NULL;
    mFieldsMask = 0;
    mStride     = 0;
    mOffset     = 0;
    mCount      = 0;
    mDataSize   = 0;
  }

  //! Initializes an accessor. An accessor can read only up to 32 floats.
  void init(domFloat_arrayRef data_src, domUint count, domUint stride, domUint offset, size_t fields_mask)
  {
    mFloatSource = data_src;
    mIntSource   = NULL;
    mBoolSource  = NULL;
    mCount       = (size_t)count;
    mStride      = (size_t)stride;
    mOffset      = (size_t)offset;
    mFieldsMask  = fields_mask;

    // count the number of scalars that will be read.
    mDataSize = 0;
    for(size_t i=0; i<32; ++i)
      if (mFieldsMask & (1<<i))
        mDataSize++;
  }

  //! Initializes an accessor. An accessor can read only up to 32 floats.
  void init(domInt_arrayRef data_src, domUint count, domUint stride, domUint offset, size_t fields_mask)
  {
    mFloatSource = NULL;
    mIntSource   = data_src;
    mBoolSource  = NULL;
    mCount       = (size_t)count;
    mStride      = (size_t)stride;
    mOffset      = (size_t)offset;
    mFieldsMask  = fields_mask;

    // count the number of scalars that will be read.
    mDataSize = 0;
    for(size_t i=0; i<32; ++i)
      if (mFieldsMask & (1<<i))
        mDataSize++;
  }

  //! Initializes an accessor. An accessor can read only up to 32 floats.
  void init(domBool_arrayRef data_src, domUint count, domUint stride, domUint offset, size_t fields_mask)
  {
    mFloatSource = NULL;
    mIntSource   = NULL;
    mBoolSource  = data_src;
    mCount       = (size_t)count;
    mStride      = (size_t)stride;
    mOffset      = (size_t)offset;
    mFieldsMask  = fields_mask;

    // count the number of scalars that will be read.
    mDataSize = 0;
    for(size_t i=0; i<32; ++i)
      if (mFieldsMask & (1<<i))
        mDataSize++;
  }

  //! Reads an element of data at the n-th position and writes it into 'output'. The number of elements that will be written can be queried by calling dataSize().
  void readData(size_t n, float* output)
  {
    size_t read_pos = mOffset + n * mStride;

    size_t pos = 0;

    if(mFloatSource)
    {
      VL_CHECK( (n < mCount) || (read_pos < mFloatSource->getValue().getCount() - mDataSize) )
      for(size_t i=0; i<32 && i<mStride; ++i)
        if (mFieldsMask & (1<<i))
          output[pos++] = (float)mFloatSource->getValue()[read_pos+i];
    }
    else
    if(mIntSource)
    {
      VL_CHECK( (n < mCount) || (read_pos < mIntSource->getValue().getCount() - mDataSize) )
      for(size_t i=0; i<32 && i<mStride; ++i)
        if (mFieldsMask & (1<<i))
          output[pos++] = (float)mIntSource->getValue()[read_pos+i];
    }
    else
    if(mBoolSource)
    {
      VL_CHECK( (n < mCount) || (read_pos < mBoolSource->getValue().getCount() - mDataSize) )
      for(size_t i=0; i<32 && i<mStride; ++i)
        if (mFieldsMask & (1<<i))
          output[pos++] = (float)mBoolSource->getValue()[read_pos+i];
    }
  }

  //! The number of elements in the source.
  size_t count() const { return mCount; }

  //! The number of elements written by readData().
  size_t dataSize() const { return mDataSize; }

protected:
  size_t mFieldsMask;
  size_t mDataSize;
  domFloat_arrayRef mFloatSource;
  domInt_arrayRef  mIntSource;
  domBool_arrayRef mBoolSource;
  size_t mStride;
  size_t mOffset;
  size_t mCount;
};
//-----------------------------------------------------------------------------
struct DaeInput: public Object
{
  DaeInput()
  {
    mSemantic = Dae::IS_UNKNOWN;
    mOffset = 0;
    mSet = 0;
  }

  ref<DaeSource> mSource;
  Dae::EInputSemantic mSemantic;
  size_t mOffset;
  size_t mSet;
};
//-----------------------------------------------------------------------------
struct DaePrimitive: public Object
{
  DaePrimitive()
  {
    mIndexStride = 0;
    mCount = 0;
    mType = Dae::PT_UNKNOWN;
  }

  Dae::EPrimitiveType mType;

  std::string mMaterial;
  std::vector< ref<DaeInput> > mChannels;
  size_t mCount;
  std::vector<domPRef> mP;
  size_t mIndexStride;
  ref<Geometry> mGeometry;
};
//-----------------------------------------------------------------------------
struct DaeMesh: public Object
{
  std::vector< ref<DaeInput> > mVertexInputs;
  std::vector< ref<DaePrimitive> > mPrimitives;
};
//-----------------------------------------------------------------------------
struct DaeNode: public Object
{
  DaeNode()
  {
    mTransform = new Transform;
  }

  ref<Transform> mTransform;
  std::vector< ref<DaeNode> > mChildren;
  std::vector< ref<DaeMesh> > mMesh;
  std::vector< ref<Actor> > mActors;
};
//-----------------------------------------------------------------------------
struct DaeSurface: public Object
{
  // mic fixme: for the moment we only support 2D images.
  ref<Image> mImage; // <init_from>
};
//-----------------------------------------------------------------------------
struct DaeSampler2D: public Object
{
  DaeSampler2D()
  {
    // default values if no tags are found.
    mMinFilter = TPF_LINEAR;
    mMagFilter = TPF_LINEAR;
    mWrapS     = TPW_REPEAT;
    mWrapT     = TPW_REPEAT;
  }

  ref<DaeSurface> mDaeSurface;    // <source>
  vl::ETexParamFilter mMinFilter; // <minfilter>
  vl::ETexParamFilter mMagFilter; // <magfilter>
  vl::ETexParamWrap   mWrapS;     // <wrap_s>
  vl::ETexParamWrap   mWrapT;     // <wrap_t>

  ref<Texture> mTexture; // actual VL texture implementing the sampler
};
//-----------------------------------------------------------------------------
struct DaeNewParam: public Object
{
  ref<DaeSampler2D> mDaeSampler2D;
  ref<DaeSurface> mDaeSurface;
  fvec4 mFloat4;
};
//-----------------------------------------------------------------------------
struct DaeColorOrTexture: public Object
{
  fvec4 mColor;
  ref<DaeSampler2D> mSampler;
  std::string mTexCoord;
};
//-----------------------------------------------------------------------------
struct DaeTechniqueCOMMON: public Object
{
  DaeTechniqueCOMMON()
  {
    mMode = Unknown;

    mEmission.mColor     = fvec4(0, 0, 0, 1);
    mAmbient.mColor      = fvec4(0, 0, 0, 1);
    mDiffuse.mColor      = fvec4(1, 1, 1, 1);
    mSpecular.mColor     = fvec4(0, 0, 0, 1);
    mShininess    = 40;

    mReflective.mColor   = fvec4(1, 1, 1, 1);
    mReflectivity = 0;

    mTransparent.mColor  = fvec4(0, 0, 0, 1);
    mOpaqueMode   = Opaque_A_ONE;
    mTransparency = 1;

    mIndexOfRefraction = 0;

    mBlendingOn = false;
  }

  enum { Unknown, Blinn, Phong, Lambert } mMode;

  DaeColorOrTexture mEmission;
  DaeColorOrTexture mAmbient;
  DaeColorOrTexture mDiffuse;
  DaeColorOrTexture mSpecular;
  float mShininess;
  DaeColorOrTexture mReflective;
  float mReflectivity;
  DaeColorOrTexture mTransparent;
  float mTransparency;
  float mIndexOfRefraction;
  bool mBlendingOn;

  enum { Opaque_A_ONE, Opaque_RGB_ZERO } mOpaqueMode;
};
//-----------------------------------------------------------------------------
struct DaeEffect: public Object
{
  DaeEffect()
  {
    mDoubleSided = false;
  }

  std::vector<ref<DaeNewParam> > mNewParams;
  ref<DaeTechniqueCOMMON> mDaeTechniqueCOMMON;
  bool mDoubleSided;
};
//-----------------------------------------------------------------------------
struct DaeMaterial: public Object
{
  ref<DaeEffect> mDaeEffect;
};
//-----------------------------------------------------------------------------
class DaeLoader
{
public:
  DaeLoader();

  bool load(VirtualFile* file);

  const ResourceDatabase* resources() const { return mResources.get(); }

  ResourceDatabase* resources() { return mResources.get(); }

  // --- options ---

  void setLoadOptions(const LoadWriterCOLLADA::LoadOptions* options) { mLoadOptions = options; }

  const LoadWriterCOLLADA::LoadOptions* loadOptions() const { return mLoadOptions; }

  // --- internal logic ---
protected:

  void reset();

  void parseInputs(DaePrimitive* dae_primitive, const domInputLocalOffset_Array& input_arr, const std::vector< ref<DaeInput> >& vertex_inputs);

  ref<DaeMesh> parseGeometry(daeElement* geometry);

  DaeSource* getSource(daeElement* source_el);

  void bindMaterials(DaeNode* dae_node, DaeMesh* dae_mesh, domBind_materialRef bind_material);

  void parseNode(daeElement* el, DaeNode* parent);

  void parseAsset(domElement* root);

  void loadImages(const domImage_Array& images);

  void parseImages(daeElement* library);

  void parseEffects(daeElement* library);

  void prepareTexture2D(DaeSampler2D* sampler2D);

  void parseMaterials(daeElement* library);

  ref<Effect> setup_vl_Effect( DaeMaterial* mat );

  static std::string percentDecode(const char* uri);

  static Dae::EInputSemantic getSemantic(const char* semantic);

  static const char* getSemanticString(Dae::EInputSemantic semantic);

  static ETexParamFilter translateSampleFilter(domFx_sampler_filter_common filter);

  static ETexParamWrap translateWrapMode(domFx_sampler_wrap_common wrap);
  
  // template required becase Transparent is implemented as something different from domCommon_color_or_texture_typeRef!!!
  template<class T_color_or_texture>
  void parseColor(const domProfile_COMMON* common, const T_color_or_texture& color_or_texture, DaeColorOrTexture* out_col);

  void generateGeometry(DaePrimitive* primitive);

protected:
  const LoadWriterCOLLADA::LoadOptions* mLoadOptions;

protected:
  ref<ResourceDatabase> mResources;
  std::map< daeElement*, ref<DaeMaterial> > mMaterials;
  std::map< daeElement*, ref<DaeEffect> > mEffects;
  std::map< daeElement*, ref<DaeMesh> > mMeshes; // daeElement* -> <geometry>
  std::vector< ref<DaeNode> > mNodes;
  std::map< daeElement*, ref<DaeSource> > mSources; // daeElement* -> <source>
  std::map< daeElement*, ref<Image> > mImages;
  std::map< daeElement*, ref<DaeNewParam> > mDaeNewParams;
  ref<Effect> mDefaultFX;
  ref<DaeNode> mScene;
  DAE mDAE;
  String mFilePath;
  fmat4 mUpMatrix;
  bool mInvertTransparency;
  bool mAssumeOpaque;
};
//-----------------------------------------------------------------------------
DaeLoader::DaeLoader()
{
  reset();

  // default material

  mDefaultFX = new Effect;
  mDefaultFX->setObjectName("<COLLADA_Default_Material>");
  mDefaultFX->shader()->enable(EN_LIGHTING);
  mDefaultFX->shader()->setRenderState( new Light(0) );
  mDefaultFX->shader()->gocMaterial()->setFlatColor( vl::fuchsia );
  mDefaultFX->shader()->gocPolygonMode()->set(PM_LINE, PM_LINE);
}
//-----------------------------------------------------------------------------
void DaeLoader::reset()
{
  mAssumeOpaque = false;
  mInvertTransparency = false;
  mScene = NULL;
  mResources = new ResourceDatabase;
}
//-----------------------------------------------------------------------------
void DaeLoader::parseInputs(DaePrimitive* dae_primitive, const domInputLocalOffset_Array& input_arr, const std::vector< ref<DaeInput> >& vertex_inputs)
{
  dae_primitive->mIndexStride = 0;

  for(size_t iinp=0; iinp<input_arr.getCount(); ++iinp)
  {
    domInputLocalOffsetRef input = input_arr.get(iinp);

    // copy over VERTEX inputs with the current offset and set
    if ( getSemantic(input->getSemantic()) ==  Dae::IS_VERTEX )
    {
      VL_CHECK(!vertex_inputs.empty())
      for(size_t ivert=0; ivert<vertex_inputs.size(); ++ivert)
      {
        ref<DaeInput> dae_input = new DaeInput;
        dae_input->mSemantic = vertex_inputs[ivert]->mSemantic;
        dae_input->mSource   = vertex_inputs[ivert]->mSource;
        dae_input->mOffset   = (size_t)input->getOffset();
        dae_input->mSet      = (size_t)input->getSet();
        dae_primitive->mChannels.push_back(dae_input);

        VL_CHECK(dae_input->mSource);
        VL_CHECK(dae_input->mSemantic != Dae::IS_UNKNOWN);

          
        dae_primitive->mIndexStride = std::max(dae_primitive->mIndexStride, dae_input->mOffset);
      }
    }
    else
    {
        ref<DaeInput> dae_input = new DaeInput;
        dae_input->mSemantic = getSemantic(input->getSemantic());
        dae_input->mSource   = getSource( input->getSource().getElement() );
        dae_input->mOffset   = (size_t)input->getOffset();
        dae_input->mSet      = (size_t)input->getSet();

        // if the source is NULL getSource() has already issued an error.
        if (dae_input->mSource)
          dae_primitive->mChannels.push_back( dae_input );

        VL_CHECK(dae_input->mSource);
        VL_CHECK(dae_input->mSemantic != Dae::IS_UNKNOWN);

        dae_primitive->mIndexStride = std::max(dae_primitive->mIndexStride, dae_input->mOffset);
    }
  }
    
  dae_primitive->mIndexStride += 1;
}
//-----------------------------------------------------------------------------
ref<DaeMesh> DaeLoader::parseGeometry(daeElement* geometry)
{
  // try to reuse the geometry in the library
  std::map< daeElement*, ref<DaeMesh> >::iterator it = mMeshes.find( geometry );
  if (it != mMeshes.end())
    return it->second;

  if (!geometry->getChild("mesh"))
    return NULL;

  domMesh* mesh = static_cast<domMesh*>(geometry->getChild("mesh"));

  // add to dictionary
  ref<DaeMesh> dae_mesh = new DaeMesh;
  mMeshes[geometry] = dae_mesh;

  // vertices
  domVerticesRef vertices = mesh->getVertices();
  domInputLocal_Array input_array = vertices->getInput_array();
  for(size_t i=0; i<input_array.getCount(); ++i)
  {
    ref<DaeInput> dae_input = new DaeInput;

    dae_input->mSemantic = getSemantic(input_array[i]->getSemantic());
    if (dae_input->mSemantic == Dae::IS_UNKNOWN)
    {
      Log::error( Say("LoadWriterCOLLADA: the following semantic is unknown: %s\n") << input_array[i]->getSemantic() );
      continue;
    }

    dae_input->mSource = getSource( input_array[i]->getSource().getElement() );
    // if the source is NULL getSource() already issued an error.
    if (!dae_input->mSource)
      continue;

    dae_mesh->mVertexInputs.push_back(dae_input);
  }

  // --- --- ---- primitives ---- --- ---

  // NOTE: for the moment we generate one Geometry for each primitive but we should try to generate
  // one single set of vertex attribute array for each input semantic and recycle it if possible.
  // Unfortunately COLLADA makes this trivial task impossible to achieve.

  // --- ---- triangles ---- ---
  domTriangles_Array triangles_arr = mesh->getTriangles_array();
  for(size_t itri=0; itri< triangles_arr.getCount(); ++itri)
  {
    domTrianglesRef triangles = triangles_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_TRIANGLES;
    dae_primitive->mCount = (size_t)triangles->getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = triangles->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    dae_primitive->mP.push_back( triangles->getP() );

    // --- ---- material ---- ---
    dae_primitive->mMaterial = triangles->getMaterial() ? triangles->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;
      
    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  // --- ---- triangles fan ---- ---
  domTrifans_Array trifan_arr = mesh->getTrifans_array();
  for(size_t itri=0; itri< trifan_arr.getCount(); ++itri)
  {
    domTrifansRef trifan = trifan_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_TRIFANS;
    dae_primitive->mCount = (size_t)trifan->getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = trifan->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    for(size_t ip=0; ip<trifan->getP_array().getCount(); ++ip)
      dae_primitive->mP.push_back( trifan->getP_array().get(ip) );

    // --- ---- material ---- ---
    dae_primitive->mMaterial = trifan->getMaterial() ? trifan->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;

    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  // --- ---- triangle strip ---- ---
  domTristrips_Array tristrip_arr = mesh->getTristrips_array();
  for(size_t itri=0; itri< tristrip_arr.getCount(); ++itri)
  {
    domTristripsRef tristrip = tristrip_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_TRISTRIPS;
    dae_primitive->mCount = (size_t)tristrip->getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = tristrip->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    for(size_t ip=0; ip<tristrip->getP_array().getCount(); ++ip)
      dae_primitive->mP.push_back( tristrip->getP_array().get(ip) );

    // --- ---- material ---- ---
    dae_primitive->mMaterial = tristrip->getMaterial() ? tristrip->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;
      
    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  // --- ---- polygons ---- ---
  domPolygons_Array polygon_arr = mesh->getPolygons_array();
  for(size_t itri=0; itri< polygon_arr.getCount(); ++itri)
  {
    domPolygonsRef polygon = polygon_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_POLYGONS;
    dae_primitive->mCount = (size_t)polygon->getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = polygon->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    for(size_t ip=0; ip<polygon->getP_array().getCount(); ++ip)
      dae_primitive->mP.push_back( polygon->getP_array().get(ip) );

    // --- ---- material ---- ---
    dae_primitive->mMaterial = polygon->getMaterial() ? polygon->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;
      
    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  // --- ---- polylists ---- ---
  domPolylist_Array polylist_arr = mesh->getPolylist_array();
  for(size_t itri=0; itri< polylist_arr.getCount(); ++itri)
  {
    domPolylistRef polylist = polylist_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_POLYGONS;
    dae_primitive->mCount = (size_t)polylist->getVcount()->getValue().getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = polylist->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    size_t ip=0;
    for(size_t ivc=0; ivc<polylist->getVcount()->getValue().getCount(); ++ivc)
    {
      domPRef p = static_cast<domP*>(domP::create(mDAE).cast());
      VL_CHECK(p->typeID() == domP::ID());
      dae_primitive->mP.push_back( p );
      size_t vcount = (size_t)polylist->getVcount()->getValue()[ivc];
      p->getValue().setCount(vcount * dae_primitive->mIndexStride);
      for(size_t i=0; i<p->getValue().getCount(); ++i)
        p->getValue().set(i, polylist->getP()->getValue()[ip++]);
    }

    // --- ---- material ---- ---
    dae_primitive->mMaterial = polylist->getMaterial() ? polylist->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;
      
    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  // --- ---- linestrips ---- ---
  domLinestrips_Array linestrip_arr = mesh->getLinestrips_array();
  for(size_t itri=0; itri< linestrip_arr.getCount(); ++itri)
  {
    domLinestripsRef linestrip = linestrip_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_LINE_STRIP;
    dae_primitive->mCount = (size_t)linestrip->getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = linestrip->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    for(size_t ip=0; ip<linestrip->getP_array().getCount(); ++ip)
      dae_primitive->mP.push_back( linestrip->getP_array().get(ip) );

    // --- ---- material ---- ---
    dae_primitive->mMaterial = linestrip->getMaterial() ? linestrip->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;
      
    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  // --- ---- lines ---- ---
  domLines_Array line_arr = mesh->getLines_array();
  for(size_t itri=0; itri< line_arr.getCount(); ++itri)
  {
    domLinesRef line = line_arr.get(itri);

    ref<DaePrimitive> dae_primitive = new DaePrimitive;
    dae_mesh->mPrimitives.push_back(dae_primitive);
    dae_primitive->mType = Dae::PT_LINES;
    dae_primitive->mCount = (size_t)line->getCount();

    // --- input ---
    domInputLocalOffset_Array input_arr = line->getInput_array();
    parseInputs(dae_primitive.get(), input_arr, dae_mesh->mVertexInputs);

    // --- ---- p ---- ---
    dae_primitive->mP.push_back( line->getP() );

    // --- ---- material ---- ---
    dae_primitive->mMaterial = line->getMaterial() ? line->getMaterial() : Dae::NO_MATERIAL_SPECIFIED;
      
    // --- ---- generates the geometry ---- ---
    generateGeometry( dae_primitive.get() );
  }

  return dae_mesh;
}
//-----------------------------------------------------------------------------
DaeSource* DaeLoader::getSource(daeElement* source_el)
{
  std::map< daeElement*, ref<DaeSource> >::iterator it = mSources.find(source_el);
  if (it != mSources.end())
    return it->second.get();
  else
  {
    VL_CHECK(source_el->typeID() == domSource::ID())
    domSourceRef source = static_cast<domSource*>(source_el);

    domSource::domTechnique_commonRef tech_common = source->getTechnique_common(); VL_CHECK(tech_common)
    domAccessorRef accessor = tech_common->getAccessor();
      
    size_t mask = 0;
    // we support up to 32 parameters for a single accessor
    domParam_Array param_array = accessor->getParam_array();
    size_t attr_count = param_array.getCount() <= 32 ? param_array.getCount() : 32;
    for(size_t ipar=0; ipar<attr_count; ++ipar)
    {
      if (param_array[ipar]->getName() && strlen(param_array[ipar]->getName()))
        mask |= 1<<ipar;
    }

    ref<DaeSource> dae_source = new DaeSource;

    if (source->getFloat_array())
      dae_source->init(source->getFloat_array(), accessor->getCount(), accessor->getStride(), accessor->getOffset(), mask);
    else
    if (source->getInt_array())
      dae_source->init(source->getInt_array(), accessor->getCount(), accessor->getStride(), accessor->getOffset(), mask);
    else
    if (source->getBool_array())
      dae_source->init(source->getBool_array(), accessor->getCount(), accessor->getStride(), accessor->getOffset(), mask);
    else
    {
      Log::error("LoadWriterCOLLADA: no supported source data found. Only Float_array, Int_array and Bool_array are supported as source data.\n");
      return NULL;
    }

    // add to source library for quick access later
    mSources[source] = dae_source;

    return dae_source.get();
  }
}
//-----------------------------------------------------------------------------
ref<Effect> DaeLoader::setup_vl_Effect( DaeMaterial* mat )
{
  VL_CHECK(mat)
  VL_CHECK(mat->mDaeEffect)
  // VL_CHECK(mat->mDaeEffect->mDaeTechniqueCOMMON)

  ref<Effect> fx = new Effect;
  fx->shader()->enable(EN_LIGHTING);
  fx->shader()->setRenderState( new Light(0) ); // mic fixme: these should be all removed and add the ones present in the scene.
  fx->shader()->enable(EN_DEPTH_TEST);

  // mic fixme: most of .dae files I tested require this even if no double_sided flag is set.
#if 1
  fx->shader()->gocLightModel()->setTwoSide(true);
  // fx->shader()->enable(EN_CULL_FACE);
#else
  if (mat->mDaeEffect->mDoubleSided)
    fx->shader()->gocLightModel()->setTwoSide(true); // yes two side lighting, no culling
  else
    fx->shader()->enable(EN_CULL_FACE); // no two sidelighting, yes culling
#endif

  // very basic material setup
  if (mat->mDaeEffect->mDaeTechniqueCOMMON)
  {
    DaeTechniqueCOMMON* common_tech =mat->mDaeEffect->mDaeTechniqueCOMMON.get();

    // material sanity checks: these are needed only when using fixed function pipeline
    common_tech->mShininess = vl::clamp(common_tech->mShininess, 0.0f, 128.0f);

    // mic fixme: this vl::Effect can be put in mDaeTechniqueCOMMON and shared among all the materials that use it.
    fx->shader()->gocMaterial()->setDiffuse  ( common_tech->mDiffuse.mColor  );
    fx->shader()->gocMaterial()->setAmbient  ( common_tech->mAmbient.mColor  );
    fx->shader()->gocMaterial()->setEmission ( common_tech->mEmission.mColor );
    fx->shader()->gocMaterial()->setSpecular ( common_tech->mSpecular.mColor );
    fx->shader()->gocMaterial()->setShininess( common_tech->mShininess );
    
    // mic fixme:
    // for the moment we ignore things like <bind_vertex_input semantic="UVSET0" input_semantic="TEXCOORD" input_set="0"/>
    // instead we use the first texture with the first texture coordinate set installed.
    if ( common_tech->mDiffuse.mSampler && common_tech->mDiffuse.mSampler->mTexture )
    {
      fx->shader()->gocTextureSampler(0)->setTexture( common_tech->mDiffuse.mSampler->mTexture.get() );
    }

    // alpha blending management

    if (mAssumeOpaque)
    {
      // sets the alpha value of all material colors, front and back
      fx->shader()->gocMaterial()->setTransparency( 1.0f );
    }
    else
    {
      float transparency = 0;
      if ( common_tech->mOpaqueMode == DaeTechniqueCOMMON::Opaque_A_ONE )
        transparency = common_tech->mTransparent.mColor.a() * common_tech->mTransparency;
      else
        transparency = (1.0f - dot( common_tech->mTransparent.mColor.rgb(), fvec3(0.2126f, 0.7152f, 0.0722f))) * common_tech->mTransparency;
      
      // sets the alpha value of all material colors, front and back
      fx->shader()->gocMaterial()->multiplyTransparency( transparency );

      // NOTE: to be pedantic with the specs we should enabled the alpha blending if common_tech->mBlendingOn == true however
      // COLLADA transparency management is historically not reliable

      // check if alpha blending is required
      float total_alpha = common_tech->mDiffuse.mColor.a()  + common_tech->mAmbient.mColor.a() +
                          common_tech->mEmission.mColor.a() + common_tech->mSpecular.mColor.a();

      // enable if material is transparent or alpha is coming from the diffuse texture
      if ( total_alpha < 1.0f || (common_tech->mTransparent.mSampler && common_tech->mTransparent.mSampler == common_tech->mDiffuse.mSampler) )
        fx->shader()->enable(EN_BLEND);
    }

  }
  else
  {
    Log::error("LoadWriterCOLLADA: technique or profile not supported.\n");
    fx->shader()->gocMaterial()->setDiffuse( vl::fuchsia );
  }

  return fx;
}
//-----------------------------------------------------------------------------
void DaeLoader::bindMaterials(DaeNode* dae_node, DaeMesh* dae_mesh, domBind_materialRef bind_material)
{
  // map symbols to actual materials
  std::map< std::string, DaeMaterial* > material_map;

  if ( bind_material )
  {
    if (bind_material->getTechnique_common())
    {
      domInstance_material_Array& material_instances = bind_material->getTechnique_common()->getInstance_material_array();
      for(size_t i=0; i<material_instances.getCount(); ++i)
      {
        daeElement* material = material_instances[i]->getTarget().getElement();
        VL_CHECK(material)
        std::map< daeElement*, ref<DaeMaterial> >::iterator it = mMaterials.find( material );
        if (it != mMaterials.end())
        {
          material_map[ material_instances[i]->getSymbol() ] = it->second.get();
        }
        else
        {
          // mic fixme: error
          continue;
        }
      }
    }
    else
    {
      // mic fixme issue error
    }
  }

  // now we need to instance the material
  for(size_t iprim=0; iprim<dae_mesh->mPrimitives.size(); ++iprim)
  {
    ref<DaeMaterial> dae_material; 

    if (!dae_mesh->mPrimitives[iprim]->mMaterial.empty())
    {
      std::map< std::string, DaeMaterial* >::iterator it = material_map.find(  dae_mesh->mPrimitives[iprim]->mMaterial );
      if (it != material_map.end())
      {
        dae_material = it->second;
      }
      else
      {
        Log::warning( Say("LoadWriterCOLLADA: material symbol %s could not be resolved.\n") << dae_mesh->mPrimitives[iprim]->mMaterial );
      }
    }

    ref<Effect> fx = dae_material ? setup_vl_Effect(dae_material.get()) : mDefaultFX;

    ref<Actor> actor = new Actor( dae_mesh->mPrimitives[iprim]->mGeometry.get(), fx.get(), dae_node->mTransform.get() );
    dae_node->mActors.push_back( actor );
  }
}
//-----------------------------------------------------------------------------
void DaeLoader::parseNode(daeElement* el, DaeNode* parent)
{
  if (el->typeID() == domNode::ID())
  {
    // --- --- --- parse this node --- --- ---

    // create new node and add it to the library
    ref<DaeNode> this_node = new DaeNode;
    mNodes.push_back(this_node);
    parent->mChildren.push_back( this_node );
    parent->mTransform->addChild( this_node->mTransform.get() );

    domNode* node = static_cast<domNode*>(el);

    // parse geometries
    domInstance_geometry_Array geometries = node->getInstance_geometry_array();
    for(size_t i=0; i<geometries.getCount(); ++i)
    {
      VL_CHECK(geometries[i]->getUrl().getElement()->typeID() == domGeometry::ID())
      daeElement* geometry = geometries[i]->getUrl().getElement();
      ref<DaeMesh> dae_mesh = parseGeometry(geometry);
      if (dae_mesh)
        this_node->mMesh.push_back(dae_mesh.get());
        
      // generate the Actors belonging to this node with their own material
      bindMaterials(this_node.get(), dae_mesh.get(), geometries[i]->getBind_material());
    }

    // parse controllers
    if (loadOptions()->extractSkins())
    {
      domInstance_controller_Array controllers = node->getInstance_controller_array();
      for(size_t i=0; i<controllers.getCount(); ++i)
      {
        VL_CHECK(controllers[i]->getUrl().getElement()->typeID() == domController::ID())
        daeElement* controller_el = controllers[i]->getUrl().getElement();
        // mic fixme issue warning
        VL_CHECK(controller_el)
        domController* controller = static_cast<domController*>(controller_el);
        daeElement* geometry = controller->getSkin()->getSource().getElement();
        // mic fixme issue warning
        VL_CHECK(geometry)
        ref<DaeMesh> dae_mesh = parseGeometry(geometry);
        if (dae_mesh)
          this_node->mMesh.push_back(dae_mesh.get());
        
        // generate the Actors belonging to this node with their own material
        bindMaterials(this_node.get(), dae_mesh.get(), controllers[i]->getBind_material());
      }
    }

    // note: transforms are post-multiplied in the order in which they are specified (as if they were sub-nodes)
    for(size_t ichild=0; ichild<node->getChildren().getCount(); ++ichild)
    {
      daeElement* child = node->getChildren()[ichild];
      
      if ( 0 == strcmp(child->getElementName(), "matrix") )
      {
        domMatrix* matrix = static_cast<domMatrix*>(child);
        mat4 local_matrix;
        for(int i=0; i<16; ++i)
          local_matrix.ptr()[i] = (Real)matrix->getValue().get(i);
        local_matrix.transpose();
        this_node->mTransform->postMultiply(local_matrix);
      }
      else
      if ( 0 == strcmp(child->getElementName(), "translate") )
      {
        domTranslate* tr = static_cast<domTranslate*>(child);
        mat4 m = mat4::getTranslation((Real)tr->getValue()[0], (Real)tr->getValue()[1], (Real)tr->getValue()[2]);
        this_node->mTransform->postMultiply( m );
      }
      else
      if ( 0 == strcmp(child->getElementName(), "rotate") )
      {
        domRotate* rot = static_cast<domRotate*>(child);
        mat4 m = mat4::getRotation((Real)rot->getValue()[3], (Real)rot->getValue()[0], (Real)rot->getValue()[1], (Real)rot->getValue()[2]);
        this_node->mTransform->postMultiply( m );
      }
      else
      if ( 0 == strcmp(child->getElementName(), "scale") )
      {
        domScale* sc = static_cast<domScale*>(child);
        mat4 m = mat4::getScaling((Real)sc->getValue()[0], (Real)sc->getValue()[1], (Real)sc->getValue()[2]);
        this_node->mTransform->postMultiply( m );
      }
      else
      if ( 0 == strcmp(child->getElementName(), "lookat") )
      {
        domLookat* lookat = static_cast<domLookat*>(child);
        vec3 eye ((Real)lookat->getValue()[0], (Real)lookat->getValue()[1], (Real)lookat->getValue()[2]);
        vec3 look((Real)lookat->getValue()[3], (Real)lookat->getValue()[4], (Real)lookat->getValue()[5]);
        vec3 up  ((Real)lookat->getValue()[6], (Real)lookat->getValue()[7], (Real)lookat->getValue()[8]);
        this_node->mTransform->preMultiply( mat4::getLookAt(eye, look, up).invert() );
      }
      else
      if ( 0 == strcmp(child->getElementName(), "skew") )
      {
        // mic fixme: support skew
        // domSkew* skew = static_cast<domSkew*>(child);
        Log::error("LoadWriterCOLLADA: <skew> transform not supported yet. Call me if you know how to compute it.\n");
      }
    }

    // --- --- --- parse children --- --- ---

    // parse instance nodes
    domInstance_node_Array nodes = node->getInstance_node_array();
    for(size_t i=0; i<nodes.getCount(); ++i)
    {
      daeElement* node = nodes[i]->getUrl().getElement();
      VL_CHECK(node->typeID() == domNode::ID())
      parseNode(node, this_node.get());
    }

    // parse proper children
    daeTArray< daeSmartRef<daeElement> > children = node->getChildren();
    for(size_t i=0; i<children.getCount(); ++i)
      parseNode(children[i], this_node.get());
  }
}
//-----------------------------------------------------------------------------
bool DaeLoader::load(VirtualFile* file)
{
  reset();

  mFilePath = file->path();

  // load COLLADA file as a string.
  std::vector<char> buffer;
  file->load(buffer);
  if (buffer.empty())
    return false;
  buffer.push_back(0);

  daeElement* root = mDAE.openFromMemory(file->path().toStdString(), (char*)&buffer[0]);
  if (!root)
  {
    Log::error( "LoadWriterCOLLADA: failed to open COLLADA document.\n" );
    return false;
  }

  parseAsset(root);

  parseImages(root->getDescendant("library_images"));

  parseEffects(root->getDescendant("library_effects"));

  parseMaterials(root->getDescendant("library_materials"));

  daeElement* visual_scene = root->getDescendant("visual_scene");
  if (!visual_scene)
  {
    Log::error( "LoadWriterCOLLADA: <visual_scene> not found!\n" );
    return false;
  }

  // --- parse the visual scene ---

  mScene = new DaeNode;
  daeTArray< daeSmartRef<daeElement> > children = visual_scene->getChildren();
  for(size_t i=0; i<children.getCount(); ++i)
    parseNode(children[i], mScene.get());

  // --- fill the resource database and final setup ---

  // --- orient geometry based on up vector ---
  // Note that we don't touch the local space vertices and the intermediate matrices,
  // for proper reorientation of matrices and geometry the up-vector conditioner in
  // Refinery should do the job.
  mScene->mTransform->preMultiply( mUpMatrix );
  // compute world matrices
  mScene->mTransform->computeWorldMatrixRecursive();

  // return the Actors
  for( size_t inode=0; inode<mNodes.size(); ++inode )
  {
    for(size_t i=0; i<mNodes[inode]->mActors.size(); ++i)
    {
      Actor* actor = mNodes[inode]->mActors[i].get();

      // add actor to the resources
      mResources->resources().push_back( actor );

      // *** flatten transform hierarchy ***
      if ( loadOptions()->flattenTransformHierarchy() )
        actor->transform()->removeFromParent();

      // *** merge draw calls ***
      if (loadOptions()->mergeDrawCallsWithTriangles())
      {
        Geometry* geom = actor->lod(0)->as<Geometry>();
        if (geom)
          geom->mergeDrawCallsWithTriangles(PT_UNKNOWN);
      }

      // *** check for transforms that require normal rescaling ***
      mat4 nmatrix = actor->transform()->worldMatrix().as3x3().invert().transpose();
      Real len_x = nmatrix.getX().length();
      Real len_y = nmatrix.getY().length();
      Real len_z = nmatrix.getZ().length();
      if ( fabs(len_x - 1) > 0.1 || fabs(len_y - 1) > 0.1 || fabs(len_z - 1) > 0.1 )
      {
        // Log::warning("Detected mesh with scaled transform: enabled normal renormalization.\n");
        if ( actor->effect()->shader()->isEnabled(vl::EN_LIGHTING) )
          actor->effect()->shader()->enable(vl::EN_NORMALIZE); // or vl::EN_RESCALE_NORMAL
      }
    }
  }

  if ( loadOptions()->flattenTransformHierarchy() )
    mScene->mTransform->eraseAllChildrenRecursive();
  else
    mResources->resources().push_back( mScene->mTransform );

  return true;
}
//-----------------------------------------------------------------------------
std::string DaeLoader::percentDecode(const char* uri)
{
  std::string str;
  for(int i=0; uri[i]; ++i)
  {
    // process encoded character
    if ( uri[i] == '%' && uri[i+1] && uri[i+2] )
    {
      ++i;
      char hex1 = uri[i];
      if (hex1 >= '0' && hex1 <= '9')
        hex1 -= '0';
      else
      if (hex1 >= 'A' && hex1 <= 'F')
        hex1 -= 'A';
      else
      if (hex1 >= 'a' && hex1 <= 'f')
        hex1 -= 'a';
      else
        hex1 = -1;

      ++i;
      char hex2 = uri[i];
      if (hex2 >= '0' && hex2 <= '9')
        hex2 -= '0';
      else
      if (hex2 >= 'A' && hex2 <= 'F')
        hex2 -= 'A';
      else
      if (hex2 >= 'a' && hex2 <= 'f')
        hex2 -= 'a';
      else
        hex2 = -1;

      // encoding error
      if (hex1 == -1 || hex2 == -1)
      {
        // insert percent code as it is
        str.push_back('%');
        i -= 2;
      }

      char ch = (hex1 << 4) + (hex2);
      str.push_back(ch);
    }
    else
      str.push_back(uri[i]);
  }
  return str;
}
//-----------------------------------------------------------------------------
void DaeLoader::loadImages(const domImage_Array& images)
{
  for(size_t i=0; i<images.getCount(); ++i)
  {
    if ( strstr( images[i]->getInit_from()->getValue().getProtocol(), "file") == 0 )
    {
      Log::error( Say("LoadWriterCOLLADA: protocol not supported: %s\n") << images[i]->getInit_from()->getValue().getURI() );
      continue;
    }

    std::string full_path = percentDecode( images[i]->getInit_from()->getValue().getURI() + 6 );
    ref<Image> image = loadImage( full_path.c_str() );
      
    mImages[ images[i].cast() ] = image;
  }
}
//-----------------------------------------------------------------------------
void DaeLoader::parseImages(daeElement* library)
{
  if (!library)
    return;
  domLibrary_images* library_images = static_cast<domLibrary_images*>(library);
  const domImage_Array& images = library_images->getImage_array();
  loadImages(images);
}
//-----------------------------------------------------------------------------
void DaeLoader::parseEffects(daeElement* library)
{
  if (!library)
    return;
  domLibrary_effects* library_effects = static_cast<domLibrary_effects*>(library);
  const domEffect_Array& effects = library_effects->getEffect_array();
  for(size_t i=0; i<effects.getCount(); ++i)
  {
    domEffect* effect = effects[i].cast();

    ref<DaeEffect> dae_effect = new DaeEffect;

    mEffects[effect] = dae_effect;

    // load images
    loadImages(effect->getImage_array());

    const domFx_profile_abstract_Array& profiles = effect->getFx_profile_abstract_array();
    for(size_t i=0; i<profiles.getCount(); ++i)
    {
      // <profile_COMMON>
      if ( profiles[i]->typeID() == domProfile_COMMON::ID() )
      {
        domProfile_COMMON* common = static_cast<domProfile_COMMON*>(profiles[i].cast());

        // --- parse <newparam> ---
        for(size_t ipar=0; ipar<common->getNewparam_array().getCount(); ++ipar)
        {
          domCommon_newparam_typeRef newparam = common->getNewparam_array()[ipar];

          ref<DaeNewParam> dae_newparam = new DaeNewParam;
          dae_effect->mNewParams.push_back( dae_newparam );

          // insert in the map se can resolve references to <sampler2D> and <surface>
          mDaeNewParams[newparam.cast()] = dae_newparam;

          // --- <surface> ---
          // mic fixme: for the moment we support only single image 2D surfaces
          if (newparam->getSurface())
          {
            domFx_surface_commonRef surface = newparam->getSurface();

            if ( !surface->getFx_surface_init_common()->getInit_from_array().getCount() )
            {
              // mic fixme: issue error
              continue;
            }

            dae_newparam->mDaeSurface = new DaeSurface;
            daeElement* ref_image = surface->getFx_surface_init_common()->getInit_from_array()[0]->getValue().getElement();
            if (!ref_image)
            {
              // mic fixme report error
              continue;
            }

            std::map< daeElement*, ref<Image> >::iterator it = mImages.find( ref_image );
            if (it != mImages.end())
              dae_newparam->mDaeSurface->mImage = it->second.get();
            else
            {
              // mic fixme: issue error
              continue;
            }
          }

          // --- <sampler2D> ---
          if (newparam->getSampler2D())
          {
            domFx_sampler2D_commonRef sampler2D = newparam->getSampler2D();
              
            dae_newparam->mDaeSampler2D = new DaeSampler2D;

            // --- <source> ---
            daeSIDResolver sid_res( effect, sampler2D->getSource()->getValue() );
            domElement* surface_newparam = sid_res.getElement();
            // mic fixme issue error
            VL_CHECK(surface_newparam);

            std::map<domElement*, ref<DaeNewParam> >::iterator it = mDaeNewParams.find(surface_newparam);
            if ( it != mDaeNewParams.end() )
            {
              dae_newparam->mDaeSampler2D->mDaeSurface = it->second->mDaeSurface;
            }
            else
            {
              // mic fixme: issue error
              continue;
            }
              
            // --- <minfilter> ---
            if( sampler2D->getMinfilter() )
            {
              dae_newparam->mDaeSampler2D->mMinFilter = translateSampleFilter( sampler2D->getMinfilter()->getValue() );
            }
                

            // --- <magfilter> ---
            if( sampler2D->getMagfilter() )
            {
              dae_newparam->mDaeSampler2D->mMagFilter = translateSampleFilter( sampler2D->getMagfilter()->getValue() );
            }

            // --- <wrap_s> ---
            if (sampler2D->getWrap_s())
            {
              dae_newparam->mDaeSampler2D->mWrapS = translateWrapMode( sampler2D->getWrap_s()->getValue() );
            }

            // --- <wrap_t> ---
            if (sampler2D->getWrap_t())
            {
              dae_newparam->mDaeSampler2D->mWrapT = translateWrapMode( sampler2D->getWrap_t()->getValue() );
            }

            // prepare vl::Texture for creation with the given parameters
            prepareTexture2D(dae_newparam->mDaeSampler2D.get());
          }

          // --- <float>, <float2>, <float3>, <floa4> ---
          if ( newparam->getFloat() )
          {
            dae_newparam->mFloat4 = fvec4((float)newparam->getFloat()->getValue(), 0, 0, 0);
          }
          else
          if ( newparam->getFloat2() )
          {
            daeDouble* fptr = &newparam->getFloat2()->getValue()[0];
            dae_newparam->mFloat4 = fvec4((float)fptr[0], (float)fptr[1], 0, 0);
          }
          else
          if ( newparam->getFloat3() )
          {
            daeDouble* fptr = &newparam->getFloat3()->getValue()[0];
            dae_newparam->mFloat4 = fvec4((float)fptr[0], (float)fptr[1], (float)fptr[2], 0);
          }
          else
          if ( newparam->getFloat4() )
          {
            daeDouble* fptr = &newparam->getFloat4()->getValue()[0];
            dae_newparam->mFloat4 = fvec4((float)fptr[0], (float)fptr[1], (float)fptr[2], (float)fptr[3]);
          }
        }

        // <technique sid="COMMON">

        // --- parse technique ---
        if (common->getTechnique()->getBlinn())
        {
          domProfile_COMMON::domTechnique::domBlinnRef blinn = common->getTechnique()->getBlinn();

          dae_effect->mDaeTechniqueCOMMON = new DaeTechniqueCOMMON;
          parseColor( common, blinn->getEmission(), &dae_effect->mDaeTechniqueCOMMON->mEmission );
          parseColor( common, blinn->getAmbient(),  &dae_effect->mDaeTechniqueCOMMON->mAmbient );
          parseColor( common, blinn->getDiffuse(),  &dae_effect->mDaeTechniqueCOMMON->mDiffuse );
          parseColor( common, blinn->getSpecular(), &dae_effect->mDaeTechniqueCOMMON->mSpecular );
          if (blinn->getShininess())
            dae_effect->mDaeTechniqueCOMMON->mShininess = (float)blinn->getShininess()->getFloat()->getValue();

          parseColor( common, blinn->getReflective(), &dae_effect->mDaeTechniqueCOMMON->mReflective );
          if (blinn->getReflectivity())
            dae_effect->mDaeTechniqueCOMMON->mReflectivity = (float)blinn->getReflectivity()->getFloat()->getValue();

          if (blinn->getTransparent())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            parseColor( common, blinn->getTransparent(), &dae_effect->mDaeTechniqueCOMMON->mTransparent );
            dae_effect->mDaeTechniqueCOMMON->mOpaqueMode = blinn->getTransparent()->getOpaque() == FX_OPAQUE_ENUM_A_ONE ? DaeTechniqueCOMMON::Opaque_A_ONE : DaeTechniqueCOMMON::Opaque_RGB_ZERO;
          }
          if (blinn->getTransparency())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            dae_effect->mDaeTechniqueCOMMON->mTransparency = (float)blinn->getTransparency()->getFloat()->getValue();
            if(mInvertTransparency)
              dae_effect->mDaeTechniqueCOMMON->mTransparency = 1.0f - dae_effect->mDaeTechniqueCOMMON->mTransparency;
          }
        }
        else
        if (common->getTechnique()->getPhong())
        {
          domProfile_COMMON::domTechnique::domPhongRef phong = common->getTechnique()->getPhong();

          dae_effect->mDaeTechniqueCOMMON = new DaeTechniqueCOMMON;
          parseColor( common, phong->getEmission(), &dae_effect->mDaeTechniqueCOMMON->mEmission );
          parseColor( common, phong->getAmbient(),  &dae_effect->mDaeTechniqueCOMMON->mAmbient );
          parseColor( common, phong->getDiffuse(),  &dae_effect->mDaeTechniqueCOMMON->mDiffuse );
          parseColor( common, phong->getSpecular(), &dae_effect->mDaeTechniqueCOMMON->mSpecular );
          if (phong->getShininess())
          {
            dae_effect->mDaeTechniqueCOMMON->mShininess = (float)phong->getShininess()->getFloat()->getValue();
          }

          parseColor( common, phong->getReflective(), &dae_effect->mDaeTechniqueCOMMON->mReflective );
          if (phong->getReflectivity())
            dae_effect->mDaeTechniqueCOMMON->mReflectivity = (float)phong->getReflectivity()->getFloat()->getValue();

          if (phong->getTransparent())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            parseColor( common, phong->getTransparent(), &dae_effect->mDaeTechniqueCOMMON->mTransparent );
            dae_effect->mDaeTechniqueCOMMON->mOpaqueMode = phong->getTransparent()->getOpaque() == FX_OPAQUE_ENUM_A_ONE ? DaeTechniqueCOMMON::Opaque_A_ONE : DaeTechniqueCOMMON::Opaque_RGB_ZERO;
          }
          if (phong->getTransparency())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            dae_effect->mDaeTechniqueCOMMON->mTransparency = (float)phong->getTransparency()->getFloat()->getValue();
            if(mInvertTransparency)
              dae_effect->mDaeTechniqueCOMMON->mTransparency = 1.0f - dae_effect->mDaeTechniqueCOMMON->mTransparency;
          }
        }
        else
        if (common->getTechnique()->getLambert())
        {
          domProfile_COMMON::domTechnique::domLambertRef lambert = common->getTechnique()->getLambert();

          dae_effect->mDaeTechniqueCOMMON = new DaeTechniqueCOMMON;
          parseColor( common, lambert->getEmission(), &dae_effect->mDaeTechniqueCOMMON->mEmission );
          parseColor( common, lambert->getAmbient(),  &dae_effect->mDaeTechniqueCOMMON->mAmbient );
          parseColor( common, lambert->getDiffuse(),  &dae_effect->mDaeTechniqueCOMMON->mDiffuse );
          dae_effect->mDaeTechniqueCOMMON->mSpecular.mColor = fvec4(0,0,0,1);
          dae_effect->mDaeTechniqueCOMMON->mShininess = 0;

          parseColor( common, lambert->getReflective(), &dae_effect->mDaeTechniqueCOMMON->mReflective );
          if (lambert->getReflectivity())
            dae_effect->mDaeTechniqueCOMMON->mReflectivity = (float)lambert->getReflectivity()->getFloat()->getValue();

          if (lambert->getTransparent())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            parseColor( common, lambert->getTransparent(), &dae_effect->mDaeTechniqueCOMMON->mTransparent );
            dae_effect->mDaeTechniqueCOMMON->mOpaqueMode = lambert->getTransparent()->getOpaque() == FX_OPAQUE_ENUM_A_ONE ? DaeTechniqueCOMMON::Opaque_A_ONE : DaeTechniqueCOMMON::Opaque_RGB_ZERO;
          }
          if (lambert->getTransparency())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            dae_effect->mDaeTechniqueCOMMON->mTransparency = (float)lambert->getTransparency()->getFloat()->getValue();
            if(mInvertTransparency)
              dae_effect->mDaeTechniqueCOMMON->mTransparency = 1.0f - dae_effect->mDaeTechniqueCOMMON->mTransparency;
          }
        }
        else
        if (common->getTechnique()->getConstant())
        {
          domProfile_COMMON::domTechnique::domConstantRef constant = common->getTechnique()->getConstant();

          dae_effect->mDaeTechniqueCOMMON = new DaeTechniqueCOMMON;
          parseColor( common, constant->getEmission(), &dae_effect->mDaeTechniqueCOMMON->mEmission );
          dae_effect->mDaeTechniqueCOMMON->mAmbient.mColor = fvec4(0,0,0,1);
          dae_effect->mDaeTechniqueCOMMON->mDiffuse.mColor = fvec4(0,0,0,1);
          dae_effect->mDaeTechniqueCOMMON->mSpecular.mColor = fvec4(0,0,0,1);
          dae_effect->mDaeTechniqueCOMMON->mShininess = 0;

          parseColor( common, constant->getReflective(), &dae_effect->mDaeTechniqueCOMMON->mReflective );
          if (constant->getReflectivity())
            dae_effect->mDaeTechniqueCOMMON->mReflectivity = (float)constant->getReflectivity()->getFloat()->getValue();

          if (constant->getTransparent())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            parseColor( common, constant->getTransparent(), &dae_effect->mDaeTechniqueCOMMON->mTransparent );
            dae_effect->mDaeTechniqueCOMMON->mOpaqueMode = constant->getTransparent()->getOpaque() == FX_OPAQUE_ENUM_A_ONE ? DaeTechniqueCOMMON::Opaque_A_ONE : DaeTechniqueCOMMON::Opaque_RGB_ZERO;
          }
          if (constant->getTransparency())
          {
            dae_effect->mDaeTechniqueCOMMON->mBlendingOn = true;
            dae_effect->mDaeTechniqueCOMMON->mTransparency = (float)constant->getTransparency()->getFloat()->getValue();
            if(mInvertTransparency)
              dae_effect->mDaeTechniqueCOMMON->mTransparency = 1.0f - dae_effect->mDaeTechniqueCOMMON->mTransparency;
          }
        }
        else
        {
          Log::error("LoadWriterCOLLADA: technique not supported.\n");
        }

        // <extra>

        for(size_t iextra=0; iextra<common->getExtra_array().getCount(); ++iextra)
        {
          domExtraRef extra = common->getExtra_array()[iextra];
          for(size_t itech=0; itech<extra->getTechnique_array().getCount(); ++itech)
          {
            domTechniqueRef tech = extra->getTechnique_array()[itech];
            if ( strstr(tech->getProfile(), "GOOGLEEARTH") )
            {
              domAny* double_sided = (domAny*)tech->getChild("double_sided");
              if (double_sided)
              {
                const char* ptr = double_sided->getValue();
                if(strcmp(ptr, "1") == 0)
                  dae_effect->mDoubleSided = true;
              }
            }
          }
        }

      }

    }
  }
}
//-----------------------------------------------------------------------------
void DaeLoader::prepareTexture2D(DaeSampler2D* sampler2D)
{
  if (sampler2D->mDaeSurface && sampler2D->mDaeSurface->mImage)
  {
    bool use_mipmaps = true;
    switch(sampler2D->mMinFilter)
    {
    case TPF_LINEAR:
    case TPF_NEAREST:
      if ( loadOptions()->useAlwaysMipmapping() )
        sampler2D->mMinFilter = TPF_LINEAR_MIPMAP_NEAREST;
      else
        use_mipmaps = false;
    }

    sampler2D->mTexture = new Texture;
    sampler2D->mTexture->prepareTexture2D(sampler2D->mDaeSurface->mImage.get(), TF_UNKNOWN, use_mipmaps, false);
    sampler2D->mTexture->getTexParameter()->setWrapS(sampler2D->mWrapS);
    sampler2D->mTexture->getTexParameter()->setWrapT(sampler2D->mWrapT);
    sampler2D->mTexture->getTexParameter()->setMinFilter(sampler2D->mMinFilter);
    sampler2D->mTexture->getTexParameter()->setMagFilter(sampler2D->mMagFilter);
  }
}
//-----------------------------------------------------------------------------
void DaeLoader::parseMaterials(daeElement* library)
{
  if (!library)
    return;
  domLibrary_materials* library_materials = static_cast<domLibrary_materials*>(library);
  const domMaterial_Array& materials = library_materials->getMaterial_array();
  for(size_t i=0; i<materials.getCount(); ++i)
  {
    domElement* effect = materials[i]->getInstance_effect()->getUrl().getElement();
    if (!effect)
    {
      // mic fixme: issue warning
      continue;
    }

    std::map< daeElement*, ref<DaeEffect> >::iterator it = mEffects.find(effect);
    if (it != mEffects.end())
    {
      domMaterial* material = materials[i].cast();
      ref<DaeMaterial> dae_material = new DaeMaterial;
      dae_material->mDaeEffect = it->second;
      mMaterials[ material ] = dae_material;
    }
    else
    {
      // mic fixme: issue warning
      continue;
    }
  }
}
//-----------------------------------------------------------------------------
Dae::EInputSemantic DaeLoader::getSemantic(const char* semantic)
{
  for(int i=0; Dae::SemanticTable[i].mSemanticString; ++i)
  {
    if (strcmp(semantic, Dae::SemanticTable[i].mSemanticString) == 0)
      return Dae::SemanticTable[i].mSemantic;
  }

  return Dae::IS_UNKNOWN;
}
//-----------------------------------------------------------------------------
const char* DaeLoader::getSemanticString(Dae::EInputSemantic semantic)
{
  for(int i=0; Dae::SemanticTable[i].mSemanticString; ++i)
  {
    if ( semantic == Dae::SemanticTable[i].mSemantic )
      return Dae::SemanticTable[i].mSemanticString;
  }

  return NULL;
}
//-----------------------------------------------------------------------------
ETexParamFilter DaeLoader::translateSampleFilter(domFx_sampler_filter_common filter)
{
  switch(filter)
  {
	  case FX_SAMPLER_FILTER_COMMON_NEAREST:                return TPF_NEAREST;
	  case FX_SAMPLER_FILTER_COMMON_LINEAR:                 return TPF_LINEAR;
	  case FX_SAMPLER_FILTER_COMMON_NEAREST_MIPMAP_NEAREST: return TPF_NEAREST_MIPMAP_NEAREST;
	  case FX_SAMPLER_FILTER_COMMON_LINEAR_MIPMAP_NEAREST:  return TPF_LINEAR_MIPMAP_NEAREST;
	  case FX_SAMPLER_FILTER_COMMON_NEAREST_MIPMAP_LINEAR:  return TPF_NEAREST_MIPMAP_LINEAR;
	  case FX_SAMPLER_FILTER_COMMON_LINEAR_MIPMAP_LINEAR:   return TPF_LINEAR_MIPMAP_LINEAR;
	  default:                                              return (ETexParamFilter)0;
  }
}
//-----------------------------------------------------------------------------
ETexParamWrap DaeLoader::translateWrapMode(domFx_sampler_wrap_common wrap)
{
  switch(wrap)
  {
	  case FX_SAMPLER_WRAP_COMMON_WRAP:   return TPW_REPEAT;
	  case FX_SAMPLER_WRAP_COMMON_MIRROR: return TPW_MIRRORED_REPEAT;
	  case FX_SAMPLER_WRAP_COMMON_CLAMP:  return TPW_CLAMP;
	  case FX_SAMPLER_WRAP_COMMON_BORDER: return TPW_CLAMP_TO_BORDER;
	  default:                            return (ETexParamWrap)0;
  }
}
//-----------------------------------------------------------------------------
template<class T_color_or_texture>
void DaeLoader::parseColor(const domProfile_COMMON* common, const T_color_or_texture& color_or_texture, DaeColorOrTexture* out_col)
{
  if (!color_or_texture)
    return;

  if (color_or_texture->getColor())
  {
    domFx_color_common& col = color_or_texture->getColor()->getValue();
    out_col->mColor = fvec4( (float)col[0], (float)col[1], (float)col[2], (float)col[3] );
  }

  if (color_or_texture->getTexture())
  {
    // <texture texture="...">
    daeSIDResolver sid_res( const_cast<domProfile_COMMON*>(common), color_or_texture->getTexture()->getTexture() );
    domElement* sampler2D_newparam = sid_res.getElement();
    // mic fixme: issue error
    VL_CHECK(sampler2D_newparam)

    std::map<domElement*, ref<DaeNewParam> >::iterator it = mDaeNewParams.find(sampler2D_newparam);
    if ( it != mDaeNewParams.end() )
    {
      VL_CHECK(it->second->mDaeSampler2D)
      out_col->mSampler = it->second->mDaeSampler2D;
    }
    else
    {
      // mic fixme: issue error
    }

    // <texture texcoord="...">
    out_col->mTexCoord = color_or_texture->getTexture()->getTexcoord();
  }
}
//-----------------------------------------------------------------------------
void DaeLoader::generateGeometry(DaePrimitive* prim)
{
  VL_CHECK(prim->mIndexStride);

  prim->mGeometry = new Geometry;

  // generate index buffers & DrawElements
  std::set<DaeVert> vert_set;
  for(size_t ip=0; ip<prim->mP.size(); ++ip)
  {
    ref<ArrayUInt1> index_buffer = new ArrayUInt1;
    index_buffer->resize( prim->mP[ip]->getValue().getCount() / prim->mIndexStride );

    for(size_t ivert=0, iidx=0; ivert<prim->mP[ip]->getValue().getCount(); ivert+=prim->mIndexStride, ++iidx)
    {
      DaeVert vert;

      const domListOfUInts& p = prim->mP[ip]->getValue();

      // fill vertex info
      for(size_t ichannel=0; ichannel<prim->mChannels.size(); ++ichannel)
        vert.mAttribIndex[ichannel] = (size_t)p[ivert + prim->mChannels[ichannel]->mOffset];

      size_t final_index = 0xFFFFFFFF;
      // retrieve/insert the vertex
      std::set<DaeVert>::iterator it = vert_set.find(vert);
      if (it == vert_set.end())
      {
        vert.mIndex = final_index = vert_set.size();
        vert_set.insert(vert);
      }
      else
        final_index = it->mIndex;
        
      // this is the actual index
      (*index_buffer)[iidx] = final_index;
    }

    // fill the DrawElementsUInt
    ref<DrawElementsUInt> de;
    switch(prim->mType)
    {
      case Dae::PT_LINES:      de = new DrawElementsUInt( PT_LINES ); break;
      case Dae::PT_LINE_STRIP: de = new DrawElementsUInt( PT_LINE_STRIP ); break;
      case Dae::PT_POLYGONS:   de = new DrawElementsUInt( PT_POLYGON ); break;
      case Dae::PT_TRIFANS:    de = new DrawElementsUInt( PT_TRIANGLE_FAN ); break;
      case Dae::PT_TRIANGLES:  de = new DrawElementsUInt( PT_TRIANGLES ); break;
      case Dae::PT_TRISTRIPS:  de = new DrawElementsUInt( PT_TRIANGLE_STRIP ); break;
      default:
        VL_TRAP()
        continue;
    }

    prim->mGeometry->drawCalls()->push_back(de.get());
    de->setIndexBuffer( index_buffer.get() );
  }

  // generate new vertex attrib info and install data
  size_t tex_unit = 0;
  for( size_t ich=0; ich<prim->mChannels.size(); ++ich )
  {
    // init data storage for this channel
    ref<ArrayAbstract> vert_attrib;
    float* ptr = NULL;
    float* ptr_end = NULL;
    switch(prim->mChannels[ich]->mSource->dataSize())
    {
      case 1:
      {
        ref<ArrayFloat1> array_f1 = new ArrayFloat1;
        vert_attrib = array_f1;
        array_f1->resize( vert_set.size() );
        ptr = array_f1->begin();
        // debug
        ptr_end = ptr + vert_set.size() * 1;
        break;
      }
      case 2:
      {
        ref<ArrayFloat2> array_f2 = new ArrayFloat2;
        vert_attrib = array_f2;
        array_f2->resize( vert_set.size() );
        ptr = array_f2->at(0).ptr();
        // debug
        ptr_end = ptr + vert_set.size() * 2;
        break;
      }
      case 3:
      {
        ref<ArrayFloat3> array_f3 = new ArrayFloat3;
        vert_attrib = array_f3;
        array_f3->resize( vert_set.size() );
        ptr = array_f3->at(0).ptr();
        // debug
        ptr_end = ptr + vert_set.size() * 3;
        break;
      }
      case 4:
      {
        ref<ArrayFloat4> array_f4 = new ArrayFloat4;
        vert_attrib = array_f4;
        array_f4->resize( vert_set.size() );
        ptr = array_f4->at(0).ptr();
        // debug
        ptr_end = ptr + vert_set.size() * 4;
        break;
      }
      default:
        Log::warning( Say("LoadWriterCOLLADA: input '%s' skipped because parameter count is more than 4.\n") << getSemanticString(prim->mChannels[ich]->mSemantic) );
        continue;
    }

    // install vertex attribute
    switch(prim->mChannels[ich]->mSemantic)
    {
    case Dae::IS_POSITION: prim->mGeometry->setVertexArray( vert_attrib.get() ); break;
    case Dae::IS_NORMAL:   prim->mGeometry->setNormalArray( vert_attrib.get() ); break;
    case Dae::IS_COLOR:    prim->mGeometry->setColorArray( vert_attrib.get() ); break;
    case Dae::IS_TEXCOORD: prim->mGeometry->setTexCoordArray( tex_unit++, vert_attrib.get() ); break;
    default:
      Log::warning( Say("LoadWriterCOLLADA: input semantic '%s' not supported.\n") << getSemanticString(prim->mChannels[ich]->mSemantic) );
      continue;
    }

    // name it as TEXCOORD@SET0 etc. to be recognized when binding (not used yet)
    vert_attrib->setObjectName( String(Say("%s@SET%n") << getSemanticString(prim->mChannels[ich]->mSemantic) << prim->mChannels[ich]->mSet).toStdString() );

    // fill the vertex attribute array
    for(std::set<DaeVert>::iterator it = vert_set.begin(); it != vert_set.end(); ++it)
    {
      const DaeVert& vert = *it;
      size_t idx = vert.mAttribIndex[ich];
      VL_CHECK(ptr + prim->mChannels[ich]->mSource->dataSize()*vert.mIndex < ptr_end);
      prim->mChannels[ich]->mSource->readData(idx, ptr +prim-> mChannels[ich]->mSource->dataSize()*vert.mIndex);
    }
  }

  // --- fix bad normals ---
  if ( loadOptions()->fixBadNormals() && prim->mGeometry->normalArray() )
  {
    ref<ArrayFloat3> norm_old = vl::cast<ArrayFloat3>(prim->mGeometry->normalArray());
    VL_CHECK(norm_old);

    // recompute normals
    prim->mGeometry->computeNormals();
    ref<ArrayFloat3> norm_new = vl::cast<ArrayFloat3>(prim->mGeometry->normalArray());
    VL_CHECK(norm_new);

    size_t flipped = 0;
    size_t degenerate = 0;
    for(size_t i=0; i<norm_new->size(); ++i)
    {
      // compare VL normals with original ones
      float l = norm_old->at(i).length();
      if ( l < 0.5f ) 
      {
        // Log::warning( Say("LoadWriterCOLLADA: degenerate normal #%n: len = %n\n") << i << l );
        norm_old->at(i) = norm_new->at(i);
        ++degenerate;
      }

      if ( l < 0.9f || l > 1.1f ) 
      {
        // mic fixme: issue these things as debug once things got stable
        Log::warning( Say("LoadWriterCOLLADA: degenerate normal #%n: len = %n\n") << i << l );
        norm_old->at(i).normalize();
        ++degenerate;
      }

      if ( dot(norm_new->at(i), norm_old->at(i)) < -0.1f )
      {
        norm_old->at(i) = -norm_old->at(i);
        ++flipped;
      }
    }

    // mic fixme: issue these things as debug once things got stable
    if (degenerate || flipped) 
      Log::warning( Say("LoadWriterCOLLADA: fixed bad normals: %n degenerate and %n flipped (out of %n).\n") << degenerate << flipped << norm_old->size() );

    // reinstall fixed normals
    prim->mGeometry->setNormalArray(norm_old.get());
  }

   // --- compute missing normals ---
   if ( loadOptions()->computeMissingNormals() && !prim->mGeometry->normalArray() )
     prim->mGeometry->computeNormals();

   // disabled: we transform the root matrix instead
   // --- orient geometry based on up vector ---
   // prim->mGeometry->transform((mat4)mUpMatrix);
}
//-----------------------------------------------------------------------------
void DaeLoader::parseAsset(domElement* root)
{
  domElement* asset_el = root->getChild("asset");
  if (asset_el)
  {
    domAsset* asset = static_cast<domAsset*>(asset_el);

    // up vector
    if (asset->getUp_axis())
    {
      if( asset->getUp_axis()->getValue() == UPAXISTYPE_X_UP )
      {
        // X_UP Negative y Positive x Positive z
        mUpMatrix.setX( fvec3( 0, 1, 0) );
        mUpMatrix.setY( fvec3(-1, 0, 0) );
        mUpMatrix.setZ( fvec3( 0, 0, 1) );
      }
      else
      if( asset->getUp_axis()->getValue() == UPAXISTYPE_Z_UP )
      {
        // Z_UP Positive x Positive z Negative y
        mUpMatrix.setX( fvec3(1, 0, 0) );
        mUpMatrix.setY( fvec3(0, 0,-1) );
        mUpMatrix.setZ( fvec3(0, 1, 0) );
      }
    }

    // try to fix the transparency written by crappy tools
    mInvertTransparency = false;
    mAssumeOpaque = false;
    if (loadOptions()->invertTransparency() == LoadWriterCOLLADA::LoadOptions::TransparencyInvert)
      mInvertTransparency = true;
    else
    if (loadOptions()->invertTransparency() == LoadWriterCOLLADA::LoadOptions::TransparencyAuto)
    {
      for(size_t i=0; i<asset->getContributor_array().getCount(); ++i)
      {
        const char* tool = asset->getContributor_array()[i]->getAuthoring_tool()->getValue();

        // mic fixme: remove
        printf("TOOL = %s\n", tool);

        // Google SketchUp before 7.1 requires <transparency> inversion.
        // see http://www.collada.org/public_forum/viewtopic.php?f=12&t=1667
        if ( tool && strstr(tool, "Google SketchUp") )
        {
          float version = 1000;
          if ( sscanf( strstr(tool, "Google SketchUp") + 16, "%f", &version) )
          {
            version = version * 100 + 0.5f;
            if (version < 710)
              mInvertTransparency = true;
          }
          break;
        }

        // See https://collada.org/mediawiki/index.php/ColladaMaya#ColladaMaya_3.03
        // - "Data exported with previous versions of our COLLADA tools may import with inverted transparency in ColladaMax 3.03 and ColladaMaya 3.03."
        // - ColladaMax/ColladaMaya before 3.03 use unpredictable combinations of <transparent> and <transparency>, so... we assume opaque.

        if ( strstr(tool, "ColladaMaya") )
        {
          float version = 1000;
          if ( sscanf( strstr(tool, "ColladaMaya") + 13, "%f", &version) )
          {
            version = version * 100 + 0.5f;
            if (version < 303)
              mAssumeOpaque = true;
          }
        }

        if ( strstr(tool, "ColladaMax") )
        {
          float version = 1000;
          if ( sscanf( strstr(tool, "ColladaMax") + 12, "%f", &version) )
          {
            version = version * 100 + 0.5f;
            if (version < 303)
              mAssumeOpaque = true;
          }
        }
      }
    }
  }
}

//-----------------------------------------------------------------------------
ref<ResourceDatabase> LoadWriterCOLLADA::load(const String& path, const LoadOptions* options)
{
  ref<VirtualFile> file = defFileSystem()->locateFile(path);

  if (file)
    return load( file.get(), options );
  else
  {
    Log::error( Say("Could not locate '%s'.\n") << path );
    return NULL;
  }
}
//-----------------------------------------------------------------------------
ref<ResourceDatabase> LoadWriterCOLLADA::load(VirtualFile* file, const LoadOptions* options)
{
  DaeLoader loader;
  loader.setLoadOptions(options);
  loader.load(file);
  return loader.resources();
}
//-----------------------------------------------------------------------------
