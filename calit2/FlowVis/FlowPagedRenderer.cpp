#include <GL/glew.h>
#include "FlowPagedRenderer.h"
#include "GLHelper.h"

#include <iostream>

#include <sys/time.h>

namespace fpr
{
#include "NormalShader.h"
#include "PlaneVecShaders.h"
#include "VortexCoreShaders.h"
}

using namespace fpr;

pthread_mutex_t FlowPagedRenderer::_glewInitLock = PTHREAD_MUTEX_INITIALIZER;
std::map<int,bool> FlowPagedRenderer::_glewInitMap;

pthread_mutex_t FlowPagedRenderer::_colorTableInitLock = PTHREAD_MUTEX_INITIALIZER;
std::map<int,GLuint> FlowPagedRenderer::_colorTableMap;

FlowPagedRenderer::FlowPagedRenderer(PagedDataSet * set, int frame, FlowVisType type, std::string attribute)
{
    _set = set;
    _currentFrame = frame;
    _nextFrame = frame;
    _type = type;
    _attribute = attribute;

    pthread_mutex_init(&_shaderInitLock,NULL);
    pthread_mutex_init(&_frameReadyLock,NULL);

    initUniData();

    //TODO: read from config
    // 1GB gpu cache
    _cache = new VBOCache(1048576);
}

FlowPagedRenderer::~FlowPagedRenderer()
{
    delete _cache;
}

void FlowPagedRenderer::frameStart(int context)
{
}

void FlowPagedRenderer::preFrame()
{
}

void FlowPagedRenderer::preDraw(int context)
{
}

void FlowPagedRenderer::draw(int context)
{
    checkGlewInit(context);
    checkShaderInit(context);
    checkColorTableInit(context);


    _cache->update(context);

    int fileID = _cache->getFileID(_set->frameFiles[_currentFrame]);

    switch(_type)
    {
	case FVT_NONE:
	{
	    GLuint surfVBO, vertsVBO, attribVBO;
	    
	    surfVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->surfaceInd.second,_set->frameList[_currentFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    vertsVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->verts.second,_set->frameList[_currentFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

	    PagedDataAttrib * attrib = NULL;
	    for(int i = 0; i < _set->frameList[_currentFrame]->pointData.size(); ++i)
	    {
		if(_set->frameList[_currentFrame]->pointData[i]->name == _attribute)
		{
		    attrib = _set->frameList[_currentFrame]->pointData[i];
		    break;
		}
	    }

	    std::vector<AttribBinding> binding;
	    std::vector<TextureBinding> texBinding;
	    std::vector<UniformBinding> uniBinding;

	    GLuint prog = 0;
	    GLuint texture = 0;

	    unsigned int unitsize;
	    if(attrib)
	    {
		AttribBinding ab;
		UniformBinding ub;
		if(attrib->attribType == VAT_VECTORS)
		{
		    ab.size = 3;
		    ab.type = GL_FLOAT;
		    unitsize = 3*sizeof(float);
		    prog = _normalVecProgram[context];

		    ub.location = _normalVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    uniBinding.push_back(ub);
		    ub.location = _normalVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    uniBinding.push_back(ub);
		}
		else if(attrib->dataType == VDT_INT)
		{
		    ab.size = 1;
		    ab.type = GL_UNSIGNED_INT;
		    unitsize = sizeof(int);
		    prog = _normalIntProgram[context];

		    ub.location = _normalIntMinUni[context];
		    ub.type = _uniDataMap["mini"].type;
		    ub.data = _uniDataMap["mini"].data;
		    uniBinding.push_back(ub);
		    ub.location = _normalIntMaxUni[context];
		    ub.type = _uniDataMap["maxi"].type;
		    ub.data = _uniDataMap["maxi"].data;
		    uniBinding.push_back(ub);
		}
		else
		{
		    ab.size = 1;
		    ab.type = GL_FLOAT;
		    unitsize = sizeof(float);
		    prog = _normalFloatProgram[context];

		    ub.location = _normalFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    uniBinding.push_back(ub);
		    ub.location = _normalFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    uniBinding.push_back(ub);
		}

		ab.index = 4;
		attribVBO = _cache->getOrRequestBuffer(context,fileID,attrib->offset,_set->frameList[_currentFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		ab.buffer = attribVBO;
		binding.push_back(ab);

		TextureBinding tb;

		pthread_mutex_lock(&_colorTableInitLock);
		tb.id = _colorTableMap[context];
		pthread_mutex_unlock(&_colorTableInitLock);

		tb.unit = 0;
		tb.type = GL_TEXTURE_1D;
		texBinding.push_back(tb);
	    }
	    else
	    {
		attribVBO = 0;
		prog = _normalProgram[context];
	    }
	    
	    //std::cerr << "CurrentFrame: " << _currentFrame << " nextFrame: " << _nextFrame << std::endl;
	    if(surfVBO && vertsVBO && (!attrib || attribVBO))
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 1.0;
		color[2] = 1.0;
		color[3] = 1.0;
		//std::cerr << "drawn" << std::endl;
		glEnable(GL_CULL_FACE);
		drawElements(GL_TRIANGLES,_set->frameList[_currentFrame]->surfaceInd.first,GL_UNSIGNED_INT,surfVBO,vertsVBO,color,binding,prog,texBinding,uniBinding);
		glDisable(GL_CULL_FACE);
	    }
	    else
	    {
		//std::cerr << "not drawn" << std::endl;
	    }

	    if(_currentFrame != _nextFrame)
	    {
		int nextfileID = _cache->getFileID(_set->frameFiles[_nextFrame]);
		GLuint ibuf, vbuf, abuf = 0;
		ibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->surfaceInd.second,_set->frameList[_nextFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		vbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->verts.second,_set->frameList[_nextFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

		PagedDataAttrib * nextattrib = NULL;
		for(int i = 0; i < _set->frameList[_nextFrame]->pointData.size(); ++i)
		{
		    if(_set->frameList[_nextFrame]->pointData[i]->name == _attribute)
		    {
			nextattrib = _set->frameList[_nextFrame]->pointData[i];
			break;
		    }
		}

		if(nextattrib)
		{
		    abuf = _cache->getOrRequestBuffer(context,nextfileID,nextattrib->offset,_set->frameList[_nextFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		}

		pthread_mutex_lock(&_frameReadyLock);
		if(ibuf && vbuf && (!attrib || abuf))
		{
		    _nextFrameReady[context] = true;
		}
		else
		{
		    _nextFrameReady[context] = false;
		}
		pthread_mutex_unlock(&_frameReadyLock);
	    }

	    break;
	}
	case FVT_ISO_SURFACE:
	{
	    GLuint indVBO, surfVBO, vertsVBO, attribVBO;
	    
	    indVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->indices.second,_set->frameList[_currentFrame]->indices.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    surfVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->surfaceInd.second,_set->frameList[_currentFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    vertsVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->verts.second,_set->frameList[_currentFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

	    PagedDataAttrib * attrib = NULL;
	    for(int i = 0; i < _set->frameList[_currentFrame]->pointData.size(); ++i)
	    {
		if(_set->frameList[_currentFrame]->pointData[i]->name == _attribute)
		{
		    attrib = _set->frameList[_currentFrame]->pointData[i];
		    break;
		}
	    }

	    std::vector<AttribBinding> surfAttribBinding;
	    std::vector<TextureBinding> surfTexBinding;
	    std::vector<UniformBinding> surfUniBinding;

	    std::vector<AttribBinding> meshAttribBinding;
	    std::vector<TextureBinding> meshTexBinding;
	    std::vector<UniformBinding> meshUniBinding;

	    GLuint surfProg = 0;
	    GLuint meshProg = 0;

	    bool drawMesh = false;

	    unsigned int unitsize;
	    if(attrib)
	    {
		AttribBinding ab;
		UniformBinding ub;
		if(attrib->attribType == VAT_VECTORS)
		{
		    ab.size = 3;
		    ab.type = GL_FLOAT;
		    unitsize = 3*sizeof(float);
		    surfProg = _normalVecProgram[context];
		    meshProg = _isoVecProgram[context];

		    ub.location = _isoVecMaxUni[context];
		    ub.type = _uniDataMap["isoMax"].type;
		    ub.data = _uniDataMap["isoMax"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _normalVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);

		    drawMesh = true;
		}
		else if(attrib->dataType == VDT_INT)
		{
		    ab.size = 1;
		    ab.type = GL_UNSIGNED_INT;
		    unitsize = sizeof(int);
		    surfProg = _normalIntProgram[context];

		    ub.location = _normalIntMinUni[context];
		    ub.type = _uniDataMap["mini"].type;
		    ub.data = _uniDataMap["mini"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalIntMaxUni[context];
		    ub.type = _uniDataMap["maxi"].type;
		    ub.data = _uniDataMap["maxi"].data;
		    surfUniBinding.push_back(ub);

		    // no mesh for int attributes
		    drawMesh = false;
		}
		else
		{
		    ab.size = 1;
		    ab.type = GL_FLOAT;
		    unitsize = sizeof(float);
		    surfProg = _normalFloatProgram[context];
		    meshProg = _isoProgram[context];

		    ub.location = _isoMaxUni[context];
		    ub.type = _uniDataMap["isoMax"].type;
		    ub.data = _uniDataMap["isoMax"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _normalFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);

		    drawMesh = true;
		}

		ab.index = 4;
		attribVBO = _cache->getOrRequestBuffer(context,fileID,attrib->offset,_set->frameList[_currentFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		ab.buffer = attribVBO;
		meshAttribBinding.push_back(ab);
		surfAttribBinding.push_back(ab);

		TextureBinding tb;

		pthread_mutex_lock(&_colorTableInitLock);
		tb.id = _colorTableMap[context];
		pthread_mutex_unlock(&_colorTableInitLock);

		tb.unit = 0;
		tb.type = GL_TEXTURE_1D;
		surfTexBinding.push_back(tb);

	    }
	    else
	    {
		attribVBO = 0;
		surfProg = _normalProgram[context];
	    }
	    
	    //std::cerr << "CurrentFrame: " << _currentFrame << " nextFrame: " << _nextFrame << std::endl;
	    if(surfVBO && vertsVBO && (!attrib || attribVBO))
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 1.0;
		color[2] = 1.0;
		color[3] = 1.0;
		glEnable(GL_CULL_FACE);
		drawElements(GL_TRIANGLES,_set->frameList[_currentFrame]->surfaceInd.first,GL_UNSIGNED_INT,surfVBO,vertsVBO,color,surfAttribBinding,surfProg,surfTexBinding,surfUniBinding);
		glDisable(GL_CULL_FACE);
	    }
	    else
	    {
		//std::cerr << "not drawn surfind: " << surfVBO << " vert: " << vertsVBO << " attrib: " << attribVBO << std::endl;
	    }

	    if(drawMesh && attribVBO && indVBO)
	    {
		std::vector<float> color(4);
		color[0] = 0.0;
		color[1] = 0.0;
		color[2] = 1.0;
		color[3] = 1.0;
		drawElements(GL_LINES_ADJACENCY,_set->frameList[_currentFrame]->indices.first,GL_UNSIGNED_INT,indVBO,vertsVBO,color,meshAttribBinding,meshProg,meshTexBinding,meshUniBinding);
	    }

	    if(_currentFrame != _nextFrame)
	    {
		int nextfileID = _cache->getFileID(_set->frameFiles[_nextFrame]);
		GLuint fullibuf, ibuf, vbuf, abuf = 0;
		fullibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->indices.second,_set->frameList[_nextFrame]->indices.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		ibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->surfaceInd.second,_set->frameList[_nextFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		vbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->verts.second,_set->frameList[_nextFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

		PagedDataAttrib * nextattrib = NULL;
		for(int i = 0; i < _set->frameList[_nextFrame]->pointData.size(); ++i)
		{
		    if(_set->frameList[_nextFrame]->pointData[i]->name == _attribute)
		    {
			nextattrib = _set->frameList[_nextFrame]->pointData[i];
			break;
		    }
		}

		if(nextattrib)
		{
		    abuf = _cache->getOrRequestBuffer(context,nextfileID,nextattrib->offset,_set->frameList[_nextFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		}


		pthread_mutex_lock(&_frameReadyLock);
		if(fullibuf && ibuf && vbuf && (!attrib || abuf))
		{
		    //std::cerr << "next frame ready. surf: " << ibuf << " vert: " << vbuf << " attrib: " << abuf << std::endl;
		    _nextFrameReady[context] = true;
		}
		else
		{
		    _nextFrameReady[context] = false;
		}
		pthread_mutex_unlock(&_frameReadyLock);
	    }

	    break;
	}
	case FVT_PLANE:
	{
	    GLuint indVBO, surfVBO, vertsVBO, attribVBO;
	    
	    indVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->indices.second,_set->frameList[_currentFrame]->indices.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    surfVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->surfaceInd.second,_set->frameList[_currentFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    vertsVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->verts.second,_set->frameList[_currentFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

	    PagedDataAttrib * attrib = NULL;
	    for(int i = 0; i < _set->frameList[_currentFrame]->pointData.size(); ++i)
	    {
		if(_set->frameList[_currentFrame]->pointData[i]->name == _attribute)
		{
		    attrib = _set->frameList[_currentFrame]->pointData[i];
		    break;
		}
	    }

	    std::vector<AttribBinding> surfAttribBinding;
	    std::vector<TextureBinding> surfTexBinding;
	    std::vector<UniformBinding> surfUniBinding;

	    std::vector<AttribBinding> meshAttribBinding;
	    std::vector<TextureBinding> meshTexBinding;
	    std::vector<UniformBinding> meshUniBinding;

	    GLuint surfProg = 0;
	    GLuint meshProg = 0;

	    bool drawMesh = false;

	    unsigned int unitsize;
	    if(attrib)
	    {
		AttribBinding ab;
		UniformBinding ub;
		if(attrib->attribType == VAT_VECTORS)
		{
		    ab.size = 3;
		    ab.type = GL_FLOAT;
		    unitsize = 3*sizeof(float);
		    surfProg = _normalVecProgram[context];
		    meshProg = _planeVecProgram[context];

		    ub.location = _normalVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);

		    ub.location = _planeVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _planeVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _planeVecAlphaUni[context];
		    ub.type = _uniDataMap["alpha"].type;
		    ub.data = _uniDataMap["alpha"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _planeVecPointUni[context];
		    ub.type = _uniDataMap["planePoint"].type;
		    ub.data = _uniDataMap["planePoint"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _planeVecNormalUni[context];
		    ub.type = _uniDataMap["planeNormal"].type;
		    ub.data = _uniDataMap["planeNormal"].data;
		    meshUniBinding.push_back(ub);

		    drawMesh = true;
		}
		else if(attrib->dataType == VDT_INT)
		{
		    ab.size = 1;
		    ab.type = GL_UNSIGNED_INT;
		    unitsize = sizeof(int);
		    surfProg = _normalIntProgram[context];

		    ub.location = _normalIntMinUni[context];
		    ub.type = _uniDataMap["mini"].type;
		    ub.data = _uniDataMap["mini"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalIntMaxUni[context];
		    ub.type = _uniDataMap["maxi"].type;
		    ub.data = _uniDataMap["maxi"].data;
		    surfUniBinding.push_back(ub);

		    // no mesh for int attributes
		    drawMesh = false;
		}
		else
		{
		    ab.size = 1;
		    ab.type = GL_FLOAT;
		    unitsize = sizeof(float);
		    surfProg = _normalFloatProgram[context];
		    meshProg = _planeFloatProgram[context];

		    ub.location = _normalFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);

		    ub.location = _planeFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _planeFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _planeFloatAlphaUni[context];
		    ub.type = _uniDataMap["alpha"].type;
		    ub.data = _uniDataMap["alpha"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _planeFloatPointUni[context];
		    ub.type = _uniDataMap["planePoint"].type;
		    ub.data = _uniDataMap["planePoint"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _planeFloatNormalUni[context];
		    ub.type = _uniDataMap["planeNormal"].type;
		    ub.data = _uniDataMap["planeNormal"].data;
		    meshUniBinding.push_back(ub);

		    drawMesh = true;
		}

		ab.index = 4;
		attribVBO = _cache->getOrRequestBuffer(context,fileID,attrib->offset,_set->frameList[_currentFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		ab.buffer = attribVBO;
		meshAttribBinding.push_back(ab);
		surfAttribBinding.push_back(ab);

		TextureBinding tb;

		pthread_mutex_lock(&_colorTableInitLock);
		tb.id = _colorTableMap[context];
		pthread_mutex_unlock(&_colorTableInitLock);

		tb.unit = 0;
		tb.type = GL_TEXTURE_1D;
		surfTexBinding.push_back(tb);
		meshTexBinding.push_back(tb);

	    }
	    else
	    {
		attribVBO = 0;
		surfProg = _normalProgram[context];
	    }
	    
	    //std::cerr << "CurrentFrame: " << _currentFrame << " nextFrame: " << _nextFrame << std::endl;
	    if(surfVBO && vertsVBO && (!attrib || attribVBO))
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 1.0;
		color[2] = 1.0;
		color[3] = 1.0;
		glEnable(GL_CULL_FACE);
		drawElements(GL_TRIANGLES,_set->frameList[_currentFrame]->surfaceInd.first,GL_UNSIGNED_INT,surfVBO,vertsVBO,color,surfAttribBinding,surfProg,surfTexBinding,surfUniBinding);
		glDisable(GL_CULL_FACE);
	    }
	    else
	    {
		//std::cerr << "not drawn surfind: " << surfVBO << " vert: " << vertsVBO << " attrib: " << attribVBO << std::endl;
	    }

	    if(drawMesh && attribVBO && indVBO)
	    {
		std::vector<float> color(4);
		color[0] = 0.0;
		color[1] = 0.0;
		color[2] = 1.0;
		color[3] = 1.0;
		drawElements(GL_LINES_ADJACENCY,_set->frameList[_currentFrame]->indices.first,GL_UNSIGNED_INT,indVBO,vertsVBO,color,meshAttribBinding,meshProg,meshTexBinding,meshUniBinding);
	    }

	    if(_currentFrame != _nextFrame)
	    {
		int nextfileID = _cache->getFileID(_set->frameFiles[_nextFrame]);
		GLuint fullibuf, ibuf, vbuf, abuf = 0;
		fullibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->indices.second,_set->frameList[_nextFrame]->indices.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		ibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->surfaceInd.second,_set->frameList[_nextFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		vbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->verts.second,_set->frameList[_nextFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

		PagedDataAttrib * nextattrib = NULL;
		for(int i = 0; i < _set->frameList[_nextFrame]->pointData.size(); ++i)
		{
		    if(_set->frameList[_nextFrame]->pointData[i]->name == _attribute)
		    {
			nextattrib = _set->frameList[_nextFrame]->pointData[i];
			break;
		    }
		}

		if(nextattrib)
		{
		    abuf = _cache->getOrRequestBuffer(context,nextfileID,nextattrib->offset,_set->frameList[_nextFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		}


		pthread_mutex_lock(&_frameReadyLock);
		if(fullibuf && ibuf && vbuf && (!attrib || abuf))
		{
		    //std::cerr << "next frame ready. surf: " << ibuf << " vert: " << vbuf << " attrib: " << abuf << std::endl;
		    _nextFrameReady[context] = true;
		}
		else
		{
		    _nextFrameReady[context] = false;
		}
		pthread_mutex_unlock(&_frameReadyLock);
	    }

	    break;
	}
	case FVT_PLANE_VEC:
	{
	    GLuint indVBO, surfVBO, vertsVBO, attribVBO;
	    
	    indVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->indices.second,_set->frameList[_currentFrame]->indices.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    surfVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->surfaceInd.second,_set->frameList[_currentFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    vertsVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->verts.second,_set->frameList[_currentFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

	    PagedDataAttrib * attrib = NULL;
	    for(int i = 0; i < _set->frameList[_currentFrame]->pointData.size(); ++i)
	    {
		if(_set->frameList[_currentFrame]->pointData[i]->name == _attribute)
		{
		    attrib = _set->frameList[_currentFrame]->pointData[i];
		    break;
		}
	    }

	    std::vector<AttribBinding> surfAttribBinding;
	    std::vector<TextureBinding> surfTexBinding;
	    std::vector<UniformBinding> surfUniBinding;

	    std::vector<AttribBinding> meshAttribBinding;
	    std::vector<TextureBinding> meshTexBinding;
	    std::vector<UniformBinding> meshUniBinding;

	    GLuint surfProg = 0;
	    GLuint meshProg = 0;

	    bool drawMesh = false;

	    unsigned int unitsize;
	    if(attrib)
	    {
		AttribBinding ab;
		UniformBinding ub;
		if(attrib->attribType == VAT_VECTORS)
		{
		    ab.size = 3;
		    ab.type = GL_FLOAT;
		    unitsize = 3*sizeof(float);
		    surfProg = _normalVecProgram[context];
		    meshProg = _vecPlaneProgram[context];

		    ub.location = _normalVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);

		    ub.location = _vecPlaneMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _vecPlaneMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _vecPlanePointUni[context];
		    ub.type = _uniDataMap["planePoint"].type;
		    ub.data = _uniDataMap["planePoint"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _vecPlaneNormalUni[context];
		    ub.type = _uniDataMap["planeNormal"].type;
		    ub.data = _uniDataMap["planeNormal"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _vecPlaneUpUni[context];
		    ub.type = _uniDataMap["planeUp"].type;
		    ub.data = _uniDataMap["planeUp"].data;
		    meshUniBinding.push_back(ub);
		    ub.location = _vecPlaneRightUni[context];
		    ub.type = _uniDataMap["planeRight"].type;
		    ub.data = _uniDataMap["planeRight"].data;
		    meshUniBinding.push_back(ub);

		    ub.location = _vecPlaneBasisInvUni[context];
		    ub.type = _uniDataMap["planeBasisInv"].type;
		    ub.data = _uniDataMap["planeBasisInv"].data;
		    meshUniBinding.push_back(ub);

		    drawMesh = true;
		}
		else if(attrib->dataType == VDT_INT)
		{
		    ab.size = 1;
		    ab.type = GL_UNSIGNED_INT;
		    unitsize = sizeof(int);
		    surfProg = _normalIntProgram[context];

		    ub.location = _normalIntMinUni[context];
		    ub.type = _uniDataMap["mini"].type;
		    ub.data = _uniDataMap["mini"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalIntMaxUni[context];
		    ub.type = _uniDataMap["maxi"].type;
		    ub.data = _uniDataMap["maxi"].data;
		    surfUniBinding.push_back(ub);

		    // no mesh for int attributes
		    drawMesh = false;
		}
		else
		{
		    ab.size = 1;
		    ab.type = GL_FLOAT;
		    unitsize = sizeof(float);
		    surfProg = _normalFloatProgram[context];

		    ub.location = _normalFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);

		    drawMesh = false;
		}

		ab.index = 4;
		attribVBO = _cache->getOrRequestBuffer(context,fileID,attrib->offset,_set->frameList[_currentFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		ab.buffer = attribVBO;
		meshAttribBinding.push_back(ab);
		surfAttribBinding.push_back(ab);

		TextureBinding tb;

		pthread_mutex_lock(&_colorTableInitLock);
		tb.id = _colorTableMap[context];
		pthread_mutex_unlock(&_colorTableInitLock);

		tb.unit = 0;
		tb.type = GL_TEXTURE_1D;
		surfTexBinding.push_back(tb);
		meshTexBinding.push_back(tb);

	    }
	    else
	    {
		attribVBO = 0;
		surfProg = _normalProgram[context];
	    }
	    
	    //std::cerr << "CurrentFrame: " << _currentFrame << " nextFrame: " << _nextFrame << std::endl;
	    if(surfVBO && vertsVBO && (!attrib || attribVBO))
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 1.0;
		color[2] = 1.0;
		color[3] = 1.0;
		glEnable(GL_CULL_FACE);
		drawElements(GL_TRIANGLES,_set->frameList[_currentFrame]->surfaceInd.first,GL_UNSIGNED_INT,surfVBO,vertsVBO,color,surfAttribBinding,surfProg,surfTexBinding,surfUniBinding);
		glDisable(GL_CULL_FACE);
	    }
	    else
	    {
		//std::cerr << "not drawn surfind: " << surfVBO << " vert: " << vertsVBO << " attrib: " << attribVBO << std::endl;
	    }

	    if(drawMesh && attribVBO && indVBO)
	    {
		std::vector<float> color(4);
		color[0] = 0.0;
		color[1] = 0.0;
		color[2] = 1.0;
		color[3] = 1.0;
		drawElements(GL_LINES_ADJACENCY,_set->frameList[_currentFrame]->indices.first,GL_UNSIGNED_INT,indVBO,vertsVBO,color,meshAttribBinding,meshProg,meshTexBinding,meshUniBinding);
	    }

	    if(_currentFrame != _nextFrame)
	    {
		int nextfileID = _cache->getFileID(_set->frameFiles[_nextFrame]);
		GLuint fullibuf, ibuf, vbuf, abuf = 0;
		fullibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->indices.second,_set->frameList[_nextFrame]->indices.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		ibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->surfaceInd.second,_set->frameList[_nextFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		vbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->verts.second,_set->frameList[_nextFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

		PagedDataAttrib * nextattrib = NULL;
		for(int i = 0; i < _set->frameList[_nextFrame]->pointData.size(); ++i)
		{
		    if(_set->frameList[_nextFrame]->pointData[i]->name == _attribute)
		    {
			nextattrib = _set->frameList[_nextFrame]->pointData[i];
			break;
		    }
		}

		if(nextattrib)
		{
		    abuf = _cache->getOrRequestBuffer(context,nextfileID,nextattrib->offset,_set->frameList[_nextFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		}


		pthread_mutex_lock(&_frameReadyLock);
		if(fullibuf && ibuf && vbuf && (!attrib || abuf))
		{
		    //std::cerr << "next frame ready. surf: " << ibuf << " vert: " << vbuf << " attrib: " << abuf << std::endl;
		    _nextFrameReady[context] = true;
		}
		else
		{
		    _nextFrameReady[context] = false;
		}
		pthread_mutex_unlock(&_frameReadyLock);
	    }

	    break;
	}
	case FVT_VORTEX_CORES:
	{
	    GLuint surfVBO, vertsVBO, attribVBO;
	    
	    surfVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->surfaceInd.second,_set->frameList[_currentFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    vertsVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->verts.second,_set->frameList[_currentFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

	    PagedDataAttrib * attrib = NULL;
	    for(int i = 0; i < _set->frameList[_currentFrame]->pointData.size(); ++i)
	    {
		if(_set->frameList[_currentFrame]->pointData[i]->name == _attribute)
		{
		    attrib = _set->frameList[_currentFrame]->pointData[i];
		    break;
		}
	    }

	    std::vector<AttribBinding> surfAttribBinding;
	    std::vector<TextureBinding> surfTexBinding;
	    std::vector<UniformBinding> surfUniBinding;

	    std::vector<AttribBinding> vcoreAttribBinding;
	    std::vector<TextureBinding> vcoreTexBinding;
	    std::vector<UniformBinding> vcoreUniBinding;

	    GLuint surfProg = 0;
	    GLuint vcoreProg = 0;

	    bool drawCores = false;
	    GLuint corePointVBO = 0, coreStrVBO = 0;

	    if(_set->frameList[_currentFrame]->vcoreVerts.first > 0 && _set->frameList[_currentFrame]->vcoreSegments.size())
	    {
		drawCores = true;

		corePointVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->vcoreVerts.second,_set->frameList[_currentFrame]->vcoreVerts.first*3*sizeof(float),GL_ARRAY_BUFFER);
		coreStrVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->vcoreStr.second,_set->frameList[_currentFrame]->vcoreStr.first*sizeof(float),GL_ARRAY_BUFFER);

		UniformBinding ub;
		ub.location = _vortexAlphaMinUni[context];
		ub.type = _uniDataMap["vmin"].type;
		ub.data = _uniDataMap["vmin"].data;
		vcoreUniBinding.push_back(ub);
		ub.location = _vortexAlphaMaxUni[context];
		ub.type = _uniDataMap["vmax"].type;
		ub.data = _uniDataMap["vmax"].data;
		vcoreUniBinding.push_back(ub);

		AttribBinding ab;
		ab.size = 1;
		ab.type = GL_FLOAT;
		ab.index = 4;
		ab.buffer = coreStrVBO;
		vcoreAttribBinding.push_back(ab);

		vcoreProg = _vortexAlphaProgram[context];
	    }

	    unsigned int unitsize;
	    if(attrib)
	    {
		AttribBinding ab;
		UniformBinding ub;
		if(attrib->attribType == VAT_VECTORS)
		{
		    ab.size = 3;
		    ab.type = GL_FLOAT;
		    unitsize = 3*sizeof(float);
		    surfProg = _normalVecProgram[context];

		    ub.location = _normalVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);
		}
		else if(attrib->dataType == VDT_INT)
		{
		    ab.size = 1;
		    ab.type = GL_UNSIGNED_INT;
		    unitsize = sizeof(int);
		    surfProg = _normalIntProgram[context];

		    ub.location = _normalIntMinUni[context];
		    ub.type = _uniDataMap["mini"].type;
		    ub.data = _uniDataMap["mini"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalIntMaxUni[context];
		    ub.type = _uniDataMap["maxi"].type;
		    ub.data = _uniDataMap["maxi"].data;
		    surfUniBinding.push_back(ub);
		}
		else
		{
		    ab.size = 1;
		    ab.type = GL_FLOAT;
		    unitsize = sizeof(float);
		    surfProg = _normalFloatProgram[context];

		    ub.location = _normalFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);
		}

		ab.index = 4;
		attribVBO = _cache->getOrRequestBuffer(context,fileID,attrib->offset,_set->frameList[_currentFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		ab.buffer = attribVBO;
		surfAttribBinding.push_back(ab);

		TextureBinding tb;

		pthread_mutex_lock(&_colorTableInitLock);
		tb.id = _colorTableMap[context];
		pthread_mutex_unlock(&_colorTableInitLock);

		tb.unit = 0;
		tb.type = GL_TEXTURE_1D;
		surfTexBinding.push_back(tb);

	    }
	    else
	    {
		attribVBO = 0;
		surfProg = _normalProgram[context];
	    }
	    
	    //std::cerr << "CurrentFrame: " << _currentFrame << " nextFrame: " << _nextFrame << std::endl;
	    if(surfVBO && vertsVBO && (!attrib || attribVBO))
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 1.0;
		color[2] = 1.0;
		color[3] = 1.0;
		glEnable(GL_CULL_FACE);
		drawElements(GL_TRIANGLES,_set->frameList[_currentFrame]->surfaceInd.first,GL_UNSIGNED_INT,surfVBO,vertsVBO,color,surfAttribBinding,surfProg,surfTexBinding,surfUniBinding);
		glDisable(GL_CULL_FACE);
	    }
	    else
	    {
		//std::cerr << "not drawn surfind: " << surfVBO << " vert: " << vertsVBO << " attrib: " << attribVBO << std::endl;
	    }

	    if(drawCores && corePointVBO && coreStrVBO)
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 0.0;
		color[2] = 0.0;
		color[3] = 0.0;

		glEnable(GL_BLEND);
		for(int i = 0; i < _set->frameList[_currentFrame]->vcoreSegments.size(); ++i)
		{
		    drawArrays(GL_LINE_STRIP,_set->frameList[_currentFrame]->vcoreSegments[i].first,_set->frameList[_currentFrame]->vcoreSegments[i].second,corePointVBO,color,vcoreAttribBinding,vcoreProg,vcoreTexBinding,vcoreUniBinding);
		}
		glDisable(GL_BLEND);
	    }

	    if(_currentFrame != _nextFrame)
	    {
		int nextfileID = _cache->getFileID(_set->frameFiles[_nextFrame]);
		GLuint ibuf, vbuf, abuf = 0, vcpbuf = 0, vcsbuf = 0;
		ibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->surfaceInd.second,_set->frameList[_nextFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		vbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->verts.second,_set->frameList[_nextFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

		PagedDataAttrib * nextattrib = NULL;
		for(int i = 0; i < _set->frameList[_nextFrame]->pointData.size(); ++i)
		{
		    if(_set->frameList[_nextFrame]->pointData[i]->name == _attribute)
		    {
			nextattrib = _set->frameList[_nextFrame]->pointData[i];
			break;
		    }
		}

		if(nextattrib)
		{
		    abuf = _cache->getOrRequestBuffer(context,nextfileID,nextattrib->offset,_set->frameList[_nextFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		}

		bool withCores = false;

		if(_set->frameList[_nextFrame]->vcoreVerts.first > 0 && _set->frameList[_nextFrame]->vcoreSegments.size())
		{
		    withCores = true;
		    vcpbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->vcoreVerts.second,_set->frameList[_nextFrame]->vcoreVerts.first*3*sizeof(float),GL_ARRAY_BUFFER);
		    vcsbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->vcoreStr.second,_set->frameList[_nextFrame]->vcoreStr.first*3*sizeof(float),GL_ARRAY_BUFFER);
		}

		pthread_mutex_lock(&_frameReadyLock);
		if(ibuf && vbuf && (!attrib || abuf) && (!withCores || (vcpbuf && vcsbuf)))
		{
		    //std::cerr << "next frame ready. surf: " << ibuf << " vert: " << vbuf << " attrib: " << abuf << std::endl;
		    _nextFrameReady[context] = true;
		}
		else
		{
		    _nextFrameReady[context] = false;
		}
		pthread_mutex_unlock(&_frameReadyLock);
	    }

	    break;
	}
	case FVT_SEP_ATT_LINES:
	{
	    GLuint surfVBO, vertsVBO, attribVBO;
	    
	    surfVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->surfaceInd.second,_set->frameList[_currentFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
	    vertsVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->verts.second,_set->frameList[_currentFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

	    PagedDataAttrib * attrib = NULL;
	    for(int i = 0; i < _set->frameList[_currentFrame]->pointData.size(); ++i)
	    {
		if(_set->frameList[_currentFrame]->pointData[i]->name == _attribute)
		{
		    attrib = _set->frameList[_currentFrame]->pointData[i];
		    break;
		}
	    }

	    std::vector<AttribBinding> surfAttribBinding;
	    std::vector<TextureBinding> surfTexBinding;
	    std::vector<UniformBinding> surfUniBinding;

	    std::vector<AttribBinding> linesAttribBinding;
	    std::vector<TextureBinding> linesTexBinding;
	    std::vector<UniformBinding> linesUniBinding;

	    GLuint surfProg = 0;
	    GLuint linesProg = 0;

	    bool drawSepLines = false;
	    bool drawAttLines = false;

	    GLuint sepPointVBO = 0, attPointVBO = 0;

	    if(_set->frameList[_currentFrame]->sepVerts.first > 0 && _set->frameList[_currentFrame]->sepSegments.size())
	    {
		drawSepLines = true;

		sepPointVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->sepVerts.second,_set->frameList[_currentFrame]->vcoreVerts.first*3*sizeof(float),GL_ARRAY_BUFFER);
	    }

	    if(_set->frameList[_currentFrame]->attVerts.first > 0 && _set->frameList[_currentFrame]->attSegments.size())
	    {
		drawAttLines = true;

		attPointVBO = _cache->getOrRequestBuffer(context,fileID,_set->frameList[_currentFrame]->attVerts.second,_set->frameList[_currentFrame]->attVerts.first*3*sizeof(float),GL_ARRAY_BUFFER);
	    }

	    unsigned int unitsize;
	    if(attrib)
	    {
		AttribBinding ab;
		UniformBinding ub;
		if(attrib->attribType == VAT_VECTORS)
		{
		    ab.size = 3;
		    ab.type = GL_FLOAT;
		    unitsize = 3*sizeof(float);
		    surfProg = _normalVecProgram[context];

		    ub.location = _normalVecMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalVecMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);
		}
		else if(attrib->dataType == VDT_INT)
		{
		    ab.size = 1;
		    ab.type = GL_UNSIGNED_INT;
		    unitsize = sizeof(int);
		    surfProg = _normalIntProgram[context];

		    ub.location = _normalIntMinUni[context];
		    ub.type = _uniDataMap["mini"].type;
		    ub.data = _uniDataMap["mini"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalIntMaxUni[context];
		    ub.type = _uniDataMap["maxi"].type;
		    ub.data = _uniDataMap["maxi"].data;
		    surfUniBinding.push_back(ub);
		}
		else
		{
		    ab.size = 1;
		    ab.type = GL_FLOAT;
		    unitsize = sizeof(float);
		    surfProg = _normalFloatProgram[context];

		    ub.location = _normalFloatMinUni[context];
		    ub.type = _uniDataMap["minf"].type;
		    ub.data = _uniDataMap["minf"].data;
		    surfUniBinding.push_back(ub);
		    ub.location = _normalFloatMaxUni[context];
		    ub.type = _uniDataMap["maxf"].type;
		    ub.data = _uniDataMap["maxf"].data;
		    surfUniBinding.push_back(ub);
		}

		ab.index = 4;
		attribVBO = _cache->getOrRequestBuffer(context,fileID,attrib->offset,_set->frameList[_currentFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		ab.buffer = attribVBO;
		surfAttribBinding.push_back(ab);

		TextureBinding tb;

		pthread_mutex_lock(&_colorTableInitLock);
		tb.id = _colorTableMap[context];
		pthread_mutex_unlock(&_colorTableInitLock);

		tb.unit = 0;
		tb.type = GL_TEXTURE_1D;
		surfTexBinding.push_back(tb);

	    }
	    else
	    {
		attribVBO = 0;
		surfProg = _normalProgram[context];
	    }
	    
	    //std::cerr << "CurrentFrame: " << _currentFrame << " nextFrame: " << _nextFrame << std::endl;
	    if(surfVBO && vertsVBO && (!attrib || attribVBO))
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 1.0;
		color[2] = 1.0;
		color[3] = 1.0;
		glEnable(GL_CULL_FACE);
		drawElements(GL_TRIANGLES,_set->frameList[_currentFrame]->surfaceInd.first,GL_UNSIGNED_INT,surfVBO,vertsVBO,color,surfAttribBinding,surfProg,surfTexBinding,surfUniBinding);
		glDisable(GL_CULL_FACE);
	    }
	    else
	    {
		//std::cerr << "not drawn surfind: " << surfVBO << " vert: " << vertsVBO << " attrib: " << attribVBO << std::endl;
	    }

	    if(drawSepLines && sepPointVBO)
	    {
		std::vector<float> color(4);
		color[0] = 1.0;
		color[1] = 0.0;
		color[2] = 0.0;
		color[3] = 0.0;

		for(int i = 0; i < _set->frameList[_currentFrame]->sepSegments.size(); ++i)
		{
		    drawArrays(GL_LINES,_set->frameList[_currentFrame]->sepSegments[i].first,_set->frameList[_currentFrame]->sepSegments[i].second,sepPointVBO,color,linesAttribBinding,linesProg,linesTexBinding,linesUniBinding);
		}
	    }

	    if(drawAttLines && attPointVBO)
	    {
		std::vector<float> color(4);
		color[0] = 0.0;
		color[1] = 1.0;
		color[2] = 0.0;
		color[3] = 0.0;

		for(int i = 0; i < _set->frameList[_currentFrame]->attSegments.size(); ++i)
		{
		    drawArrays(GL_LINES,_set->frameList[_currentFrame]->attSegments[i].first,_set->frameList[_currentFrame]->attSegments[i].second,attPointVBO,color,linesAttribBinding,linesProg,linesTexBinding,linesUniBinding);
		}
	    }

	    if(_currentFrame != _nextFrame)
	    {
		int nextfileID = _cache->getFileID(_set->frameFiles[_nextFrame]);
		GLuint ibuf, vbuf, abuf = 0, attbuf = 0, sepbuf = 0;
		ibuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->surfaceInd.second,_set->frameList[_nextFrame]->surfaceInd.first*sizeof(unsigned int),GL_ELEMENT_ARRAY_BUFFER);
		vbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->verts.second,_set->frameList[_nextFrame]->verts.first*3*sizeof(float),GL_ARRAY_BUFFER);

		PagedDataAttrib * nextattrib = NULL;
		for(int i = 0; i < _set->frameList[_nextFrame]->pointData.size(); ++i)
		{
		    if(_set->frameList[_nextFrame]->pointData[i]->name == _attribute)
		    {
			nextattrib = _set->frameList[_nextFrame]->pointData[i];
			break;
		    }
		}

		if(nextattrib)
		{
		    abuf = _cache->getOrRequestBuffer(context,nextfileID,nextattrib->offset,_set->frameList[_nextFrame]->verts.first*unitsize,GL_ARRAY_BUFFER);
		}

		bool withSep = false;
		bool withAtt = false;

		if(_set->frameList[_nextFrame]->sepVerts.first > 0 && _set->frameList[_nextFrame]->sepSegments.size())
		{
		    withSep = true;
		    sepbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->sepVerts.second,_set->frameList[_nextFrame]->sepVerts.first*3*sizeof(float),GL_ARRAY_BUFFER);
		}

		if(_set->frameList[_nextFrame]->attVerts.first > 0 && _set->frameList[_nextFrame]->attSegments.size())
		{
		    withAtt = true;
		    attbuf = _cache->getOrRequestBuffer(context,nextfileID,_set->frameList[_nextFrame]->attVerts.second,_set->frameList[_nextFrame]->attVerts.first*3*sizeof(float),GL_ARRAY_BUFFER);
		}

		pthread_mutex_lock(&_frameReadyLock);
		if(ibuf && vbuf && (!attrib || abuf) && (!withSep || sepbuf) && (!withAtt || attbuf))
		{
		    _nextFrameReady[context] = true;
		}
		else
		{
		    _nextFrameReady[context] = false;
		}
		pthread_mutex_unlock(&_frameReadyLock);
	    }

	    break;
	}
	default:
	    break;
    }
}

void FlowPagedRenderer::postFrame()
{
    _cache->advanceTime();
}

void FlowPagedRenderer::setType(FlowVisType type, std::string attribute)
{
    _type = type;
    _attribute = attribute;
    pthread_mutex_lock(&_frameReadyLock);

    for(std::map<int,bool>::iterator it = _nextFrameReady.begin(); it != _nextFrameReady.end(); ++it)
    {
	it->second = false;
    }

    pthread_mutex_unlock(&_frameReadyLock);
}

FlowVisType FlowPagedRenderer::getType()
{
    return _type;
}

std::string FlowPagedRenderer::getAttribute()
{
    return _attribute;
}

void FlowPagedRenderer::setNextFrame(int frame)
{
    _nextFrame = frame;
    pthread_mutex_lock(&_frameReadyLock);

    for(std::map<int,bool>::iterator it = _nextFrameReady.begin(); it != _nextFrameReady.end(); ++it)
    {
	it->second = false;
    }

    pthread_mutex_unlock(&_frameReadyLock);
}

bool FlowPagedRenderer::advance()
{
    if(_currentFrame != _nextFrame)
    {
	pthread_mutex_lock(&_frameReadyLock);
	bool advance = true;
	for(std::map<int,bool>::iterator it = _nextFrameReady.begin(); it != _nextFrameReady.end(); ++it)
	{
	    if(!it->second)
	    {
		advance = false;
	    }
	}

	if(advance)
	{
	    _currentFrame = _nextFrame;

	    for(std::map<int,bool>::iterator it = _nextFrameReady.begin(); it != _nextFrameReady.end(); ++it)
	    {
		it->second = false;
	    }
	}

	pthread_mutex_unlock(&_frameReadyLock);

	return advance;
    }
    return true;
}

void FlowPagedRenderer::setUniData(std::string key, struct UniData & data)
{
    if(_uniDataMap.find(key) != _uniDataMap.end())
    {
	deleteUniData(_uniDataMap[key]);
    }
    _uniDataMap[key] = data;
}

bool FlowPagedRenderer::getUniData(std::string key, struct UniData & data)
{
    if(_uniDataMap.find(key) != _uniDataMap.end())
    {
	data = _uniDataMap[key];
	return true;
    }
    return false;
}

void FlowPagedRenderer::freeResources(int context)
{
    _cache->update(context);
    _cache->freeResources(context);
}

bool FlowPagedRenderer::freeDone()
{
    return _cache->freeDone();
}

// create all uniform data here, so it doesn't need to be checked for every time
void FlowPagedRenderer::initUniData()
{
    _uniDataMap["minf"].type = UNI_FLOAT;
    _uniDataMap["minf"].data = new float[1];
    _uniDataMap["maxf"].type = UNI_FLOAT;
    _uniDataMap["maxf"].data = new float[1];
    _uniDataMap["mini"].type = UNI_INT;
    _uniDataMap["mini"].data = new int[1];
    _uniDataMap["maxi"].type = UNI_INT;
    _uniDataMap["maxi"].data = new int[1];

    _uniDataMap["isoMax"].type = UNI_FLOAT;
    _uniDataMap["isoMax"].data = new float[1];

    _uniDataMap["planePoint"].type = UNI_FLOAT3;
    _uniDataMap["planePoint"].data = new float[3];
    _uniDataMap["planeNormal"].type = UNI_FLOAT3;
    _uniDataMap["planeNormal"].data = new float[3];
    _uniDataMap["alpha"].type = UNI_FLOAT;
    _uniDataMap["alpha"].data = new float[1];

    _uniDataMap["planeUp"].type = UNI_FLOAT3;
    _uniDataMap["planeUp"].data = new float[3];
    _uniDataMap["planeRight"].type = UNI_FLOAT3;
    _uniDataMap["planeRight"].data = new float[3];
    _uniDataMap["planeBasisInv"].type = UNI_MAT3;
    _uniDataMap["planeBasisInv"].data = new float[9];

    _uniDataMap["vmin"].type = UNI_FLOAT;
    _uniDataMap["vmin"].data = new float[1];
    _uniDataMap["vmax"].type = UNI_FLOAT;
    _uniDataMap["vmax"].data = new float[1];
}

void FlowPagedRenderer::checkGlewInit(int context)
{
    pthread_mutex_lock(&_glewInitLock);

    if(!_glewInitMap[context])
    {
	glewInit();
	_glewInitMap[context] = true;
    }

    pthread_mutex_unlock(&_glewInitLock);

    pthread_mutex_lock(&_frameReadyLock);

    _nextFrameReady[context] = false;

    pthread_mutex_unlock(&_frameReadyLock);
}

void FlowPagedRenderer::checkShaderInit(int context)
{
    pthread_mutex_lock(&_shaderInitLock);

    if(!_shaderInitMap[context])
    {
	GLuint verts, frags, geoms;
	createShaderFromSrc(normalVertSrc,GL_VERTEX_SHADER,verts,"NormalVert");
	createShaderFromSrc(normalGeomSrc,GL_GEOMETRY_SHADER,geoms,"NormalGeom");
	createShaderFromSrc(normalFragSrc,GL_FRAGMENT_SHADER,frags,"NormalFrag");
	createProgram(_normalProgram[context],verts,frags,geoms,GL_TRIANGLES,GL_TRIANGLE_STRIP,3);

	createShaderFromSrc(normalFloatVertSrc,GL_VERTEX_SHADER,verts,"NormalFloatVert");
	createShaderFromSrc(normalFloatGeomSrc,GL_GEOMETRY_SHADER,geoms,"NormalFloatGeom");
	createShaderFromSrc(normalFloatFragSrc,GL_FRAGMENT_SHADER,frags,"NormalFloatFrag");
	createProgram(_normalFloatProgram[context],verts,frags,geoms,GL_TRIANGLES,GL_TRIANGLE_STRIP,3);

	_normalFloatMinUni[context] = glGetUniformLocation(_normalFloatProgram[context],"min");
	_normalFloatMaxUni[context] = glGetUniformLocation(_normalFloatProgram[context],"max");

	createShaderFromSrc(normalIntVertSrc,GL_VERTEX_SHADER,verts,"NormalIntVert");
	createShaderFromSrc(normalFloatGeomSrc,GL_GEOMETRY_SHADER,geoms,"NormalFloatGeom");
	createShaderFromSrc(normalFloatFragSrc,GL_FRAGMENT_SHADER,frags,"NormalFloatFrag");
	createProgram(_normalIntProgram[context],verts,frags,geoms,GL_TRIANGLES,GL_TRIANGLE_STRIP,3);

	_normalIntMinUni[context] = glGetUniformLocation(_normalIntProgram[context],"min");
	_normalIntMaxUni[context] = glGetUniformLocation(_normalIntProgram[context],"max");

	createShaderFromSrc(normalVecVertSrc,GL_VERTEX_SHADER,verts,"NormalVecVert");
	createShaderFromSrc(normalFloatGeomSrc,GL_GEOMETRY_SHADER,geoms,"NormalFloatGeom");
	createShaderFromSrc(normalFloatFragSrc,GL_FRAGMENT_SHADER,frags,"NormalFloatFrag");
	createProgram(_normalVecProgram[context],verts,frags,geoms,GL_TRIANGLES,GL_TRIANGLE_STRIP,3);

	_normalVecMinUni[context] = glGetUniformLocation(_normalVecProgram[context],"min");
	_normalVecMaxUni[context] = glGetUniformLocation(_normalVecProgram[context],"max");

	createShaderFromSrc(isoFloatVertSrc,GL_VERTEX_SHADER,verts,"isoFloatVert");
	createShaderFromSrc(isoGeomSrc,GL_GEOMETRY_SHADER,geoms,"isoGeom");
	createShaderFromSrc(isoFragSrc,GL_FRAGMENT_SHADER,frags,"isoFrag");
	createProgram(_isoProgram[context],verts,frags,geoms,GL_LINES_ADJACENCY,GL_TRIANGLE_STRIP,4);

	_isoMaxUni[context] = glGetUniformLocation(_isoProgram[context],"isoMax");

	createShaderFromSrc(isoVecVertSrc,GL_VERTEX_SHADER,verts,"isoVecVert");
	createShaderFromSrc(isoGeomSrc,GL_GEOMETRY_SHADER,geoms,"isoGeom");
	createShaderFromSrc(isoFragSrc,GL_FRAGMENT_SHADER,frags,"isoFrag");
	createProgram(_isoVecProgram[context],verts,frags,geoms,GL_LINES_ADJACENCY,GL_TRIANGLE_STRIP,4);

	_isoVecMaxUni[context] = glGetUniformLocation(_isoVecProgram[context],"isoMax");

	createShaderFromSrc(planeFloatVertSrc,GL_VERTEX_SHADER,verts,"planeFloatVert");
	createShaderFromSrc(planeGeomSrc,GL_GEOMETRY_SHADER,geoms,"planeGeom");
	createShaderFromSrc(planeFragSrc,GL_FRAGMENT_SHADER,frags,"planeFrag");
	createProgram(_planeFloatProgram[context],verts,frags,geoms,GL_LINES_ADJACENCY,GL_TRIANGLE_STRIP,4);

	_planeFloatMinUni[context] = glGetUniformLocation(_planeFloatProgram[context],"min");
	_planeFloatMaxUni[context] = glGetUniformLocation(_planeFloatProgram[context],"max");
	_planeFloatPointUni[context] = glGetUniformLocation(_planeFloatProgram[context],"planePoint");
	_planeFloatNormalUni[context] = glGetUniformLocation(_planeFloatProgram[context],"planeNormal");
	_planeFloatAlphaUni[context] = glGetUniformLocation(_planeFloatProgram[context],"alpha");

	createShaderFromSrc(planeVecVertSrc,GL_VERTEX_SHADER,verts,"planeVecVert");
	createShaderFromSrc(planeGeomSrc,GL_GEOMETRY_SHADER,geoms,"planeGeom");
	createShaderFromSrc(planeFragSrc,GL_FRAGMENT_SHADER,frags,"planeFrag");
	createProgram(_planeVecProgram[context],verts,frags,geoms,GL_LINES_ADJACENCY,GL_TRIANGLE_STRIP,4);

	_planeVecMinUni[context] = glGetUniformLocation(_planeVecProgram[context],"min");
	_planeVecMaxUni[context] = glGetUniformLocation(_planeVecProgram[context],"max");
	_planeVecPointUni[context] = glGetUniformLocation(_planeVecProgram[context],"planePoint");
	_planeVecNormalUni[context] = glGetUniformLocation(_planeVecProgram[context],"planeNormal");
	_planeVecAlphaUni[context] = glGetUniformLocation(_planeVecProgram[context],"alpha");

	createShaderFromSrc(vecPlaneVertSrc,GL_VERTEX_SHADER,verts,"vecPlaneVert");
	createShaderFromSrc(vecPlaneGeomSrc,GL_GEOMETRY_SHADER,geoms,"vecPlaneGeom");
	createShaderFromSrc(vecPlaneFragSrc,GL_FRAGMENT_SHADER,frags,"vecPlaneFrag");
	createProgram(_vecPlaneProgram[context],verts,frags,geoms,GL_LINES_ADJACENCY,GL_LINE_STRIP,200);

	_vecPlaneMinUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"min");
	_vecPlaneMaxUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"max");
	_vecPlanePointUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"planePoint");
	_vecPlaneNormalUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"planeNormal");
	_vecPlaneUpUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"planeUp");
	_vecPlaneRightUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"planeRight");
	_vecPlaneBasisInvUni[context] = glGetUniformLocation(_vecPlaneProgram[context],"planeBasisInv");

	createShaderFromSrc(vcoreAlphaVertSrc,GL_VERTEX_SHADER,verts,"vcoreAlphaVert");
	createProgram(_vortexAlphaProgram[context],verts,0);

	_vortexAlphaMinUni[context] = glGetUniformLocation(_vortexAlphaProgram[context],"min");
	_vortexAlphaMaxUni[context] = glGetUniformLocation(_vortexAlphaProgram[context],"max");

	_shaderInitMap[context] = true;
    }

    pthread_mutex_unlock(&_shaderInitLock);
}

void FlowPagedRenderer::checkColorTableInit(int context)
{
    pthread_mutex_lock(&_colorTableInitLock);

    if(!_colorTableMap[context])
    {
	glGenTextures(1,&_colorTableMap[context]);
	int size = 32;
	std::vector<float> colorR;
	std::vector<float> colorG;
	std::vector<float> colorB;
	colorR.push_back(0.0);
	colorR.push_back(0.7);
	colorR.push_back(0.7);
	colorG.push_back(0.0);
	colorG.push_back(0.7);
	colorG.push_back(0.0);
	colorB.push_back(0.7);
	colorB.push_back(0.7);
	colorB.push_back(0.0);

	unsigned char * data = new unsigned char[size*3];

	for(int i = 0; i < size; ++i)
	{
	    float pos = ((float)i) / ((float)(size-1));
	    pos = fmax(pos,0.0);
	    pos = fmin(pos,1.0);
	    pos = pos * ((float)(colorR.size()-1));
	    int topIndex = ceil(pos);
	    if(topIndex >= colorR.size())
	    {
		topIndex = colorR.size() - 1;
	    }
	    int bottomIndex = floor(pos);
	    if(bottomIndex < 0)
	    {
		bottomIndex = 0;
	    }

	    float ratio = pos - floor(pos);
	    data[(3*i)+0] = (unsigned char)((colorR[bottomIndex] * (1.0 - ratio) + colorR[topIndex] * ratio) * 255.0);
	    data[(3*i)+1] = (unsigned char)((colorG[bottomIndex] * (1.0 - ratio) + colorG[topIndex] * ratio) * 255.0);
	    data[(3*i)+2] = (unsigned char)((colorB[bottomIndex] * (1.0 - ratio) + colorB[topIndex] * ratio) * 255.0);
	    //std::cerr << "color: " << (int)data[(3*i)+0] << " " << (int)data[(3*i)+1] << " " << (int)data[(3*i)+2] << std::endl;
	}

	glBindTexture(GL_TEXTURE_1D,_colorTableMap[context]);

	glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	glTexImage1D(GL_TEXTURE_1D,0,3,size,0,GL_RGB,GL_UNSIGNED_BYTE,data);

	glBindTexture(GL_TEXTURE_1D,0);

	delete[] data;
    }

    pthread_mutex_unlock(&_colorTableInitLock);
}

void FlowPagedRenderer::deleteUniData(UniData & data)
{
    switch(data.type)
    {
	case UNI_FLOAT:
	case UNI_FLOAT3:
	case UNI_MAT3:
	    delete[] (float*)data.data;
	    break;
	case UNI_INT:
	    delete[] (int*)data.data;
	    break;
	case UNI_UINT:
	    delete[] (unsigned int*)data.data;
	    break;
	default:
	    std::cerr << "Warning: trying to delete data for unknown uniform type." << std::endl;
	    break;
    }
}

void FlowPagedRenderer::loadUniform(UniformBinding & uni)
{
    switch(uni.type)
    {
	case UNI_FLOAT:
	    glUniform1fv(uni.location,1,(GLfloat*)uni.data);
	    break;
	case UNI_FLOAT3:
	    glUniform3fv(uni.location,1,(GLfloat*)uni.data);
	    break;
	case UNI_MAT3:
	    glUniformMatrix3fv(uni.location,1,false,(GLfloat*)uni.data);
	    break;
	case UNI_INT:
	    glUniform1iv(uni.location,1,(GLint*)uni.data);
	    break;
	case UNI_UINT:
	    glUniform1uiv(uni.location,1,(GLuint*)uni.data);
	    break;
	default:
	    std::cerr << "Warning: trying to load uniform of unknown type." << std::endl;
	    break;
    }
}

void FlowPagedRenderer::drawElements(GLenum mode, GLsizei count, GLenum type, GLuint indVBO, GLuint vertsVBO, std::vector<float> & color, std::vector<FlowPagedRenderer::AttribBinding> & attribBinding, GLuint program, std::vector<TextureBinding> & textureBinding, std::vector<UniformBinding> & uniBinding)
{
    glEnableClientState(GL_VERTEX_ARRAY);

    if(color.size() < 4)
    {
	glColor4f(1.0,1.0,1.0,1.0);
    }
    else
    {
	glColor4f(color[0],color[1],color[2],color[3]);
    }

    glBindBuffer(GL_ARRAY_BUFFER,vertsVBO);
    glVertexPointer(3,GL_FLOAT,0,0);

    for(int i = 0; i < attribBinding.size(); ++i)
    {
	glEnableVertexAttribArray(attribBinding[i].index);
	glBindBuffer(GL_ARRAY_BUFFER,attribBinding[i].buffer);
	if(attribBinding[i].type == GL_FLOAT)
	{
	    glVertexAttribPointer(attribBinding[i].index,attribBinding[i].size,attribBinding[i].type,GL_FALSE,0,0);
	}
	else
	{
	    glVertexAttribIPointer(attribBinding[i].index,attribBinding[i].size,attribBinding[i].type,0,0);
	}
    }

    glBindBuffer(GL_ARRAY_BUFFER,0);

    for(int i = 0; i < textureBinding.size(); ++i)
    {
	//std::cerr << "binding texture unit: " << textureBinding[i].unit << " id: " << textureBinding[i].id << std::endl;
	glActiveTexture(GL_TEXTURE0 + textureBinding[i].unit);
	glBindTexture(textureBinding[i].type,textureBinding[i].id);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,indVBO);
    glUseProgram(program);

    for(int i = 0; i < uniBinding.size(); ++i)
    {
	loadUniform(uniBinding[i]);
    }

    glDrawElements(mode,count,type,0);
    glUseProgram(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);

    for(int i = 0; i < textureBinding.size(); ++i)
    {
	glActiveTexture(GL_TEXTURE0 + textureBinding[i].unit);
	glBindTexture(GL_TEXTURE_1D,0);
    }
    if(textureBinding.size())
    {
	glActiveTexture(GL_TEXTURE0);
    }

    for(int i = 0; i < attribBinding.size(); ++i)
    {
	glDisableVertexAttribArray(attribBinding[i].index);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
}

void FlowPagedRenderer::drawArrays(GLenum mode, GLint first, GLsizei count, GLuint vertsVBO, std::vector<float> & color, std::vector<AttribBinding> & attribBinding, GLuint program, std::vector<TextureBinding> & textureBinding, std::vector<UniformBinding> & uniBinding)
{
    glEnableClientState(GL_VERTEX_ARRAY);

    if(color.size() < 4)
    {
	glColor4f(1.0,1.0,1.0,1.0);
    }
    else
    {
	glColor4f(color[0],color[1],color[2],color[3]);
    }

    glBindBuffer(GL_ARRAY_BUFFER,vertsVBO);
    glVertexPointer(3,GL_FLOAT,0,0);

    for(int i = 0; i < attribBinding.size(); ++i)
    {
	glEnableVertexAttribArray(attribBinding[i].index);
	glBindBuffer(GL_ARRAY_BUFFER,attribBinding[i].buffer);
	if(attribBinding[i].type == GL_FLOAT)
	{
	    glVertexAttribPointer(attribBinding[i].index,attribBinding[i].size,attribBinding[i].type,GL_FALSE,0,0);
	}
	else
	{
	    glVertexAttribIPointer(attribBinding[i].index,attribBinding[i].size,attribBinding[i].type,0,0);
	}
    }

    glBindBuffer(GL_ARRAY_BUFFER,0);

    for(int i = 0; i < textureBinding.size(); ++i)
    {
	//std::cerr << "binding texture unit: " << textureBinding[i].unit << " id: " << textureBinding[i].id << std::endl;
	glActiveTexture(GL_TEXTURE0 + textureBinding[i].unit);
	glBindTexture(textureBinding[i].type,textureBinding[i].id);
    }

    glUseProgram(program);

    for(int i = 0; i < uniBinding.size(); ++i)
    {
	loadUniform(uniBinding[i]);
    }

    glDrawArrays(mode,first,count);
    glUseProgram(0);

    for(int i = 0; i < textureBinding.size(); ++i)
    {
	glActiveTexture(GL_TEXTURE0 + textureBinding[i].unit);
	glBindTexture(GL_TEXTURE_1D,0);
    }
    if(textureBinding.size())
    {
	glActiveTexture(GL_TEXTURE0);
    }

    for(int i = 0; i < attribBinding.size(); ++i)
    {
	glDisableVertexAttribArray(attribBinding[i].index);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
}
