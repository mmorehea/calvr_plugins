#include "TextShape.h"

#include <osg/Geometry>
#include <osg/Material>

#include <string>
#include <vector>
#include <iostream>

TextShape::TextShape(std::string command, std::string name) 
{
    _type = SimpleShape::TEXT;

    BasicShape::setName(name);
    setColor(osg::Vec4(1.0, 1.0, 1.0, 1.0));
    setCharacterSize(10.0f);
    setFontResolution(40,40);
    setFont ("/usr/share/fonts/liberation/LiberationSans-Regular.ttf");
    setAxisAlignment(osgText::TextBase::XZ_PLANE);
    setBackdropType(osgText::Text::NONE);

    osg::StateSet* state = osgText::Text::getOrCreateStateSet();
    state->setMode(GL_BLEND, osg::StateAttribute::ON);
    osg::Material* mat = new osg::Material();
    mat->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
    state->setAttributeAndModes(mat, osg::StateAttribute::ON);
    
    update(command);
}

TextShape::~TextShape()
{
}

void TextShape::update(std::string command)
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
    _dirty = true;

    // check for changed values
    addParameter(command, "x");
    addParameter(command, "y");
    addParameter(command, "z");
    addParameter(command, "r");
    addParameter(command, "g");
    addParameter(command, "b");
    addParameter(command, "a");
    addParameter(command, "size");
    addParameter(command, "label");
}

void TextShape::update()
{
	OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
    if( !_dirty )
        return;

    osg::Vec3 p = getPosition();
    osg::Vec4 c = getColor();
    float size = getCharacterHeight();
    std::string text = getText().createUTF8EncodedString();

    setParameter("x", p.x()); 
    setParameter("y", p.y()); 
    setParameter("z", p.z()); 
    setParameter("r", c.r()); 
    setParameter("g", c.g()); 
    setParameter("b", c.b()); 
    setParameter("a", c.a());
    setParameter("size", size);
    setParameter("label", text);
    
    setText(text);
    setCharacterSize(size);
    setPosition(p);
    setColor(c);

    osgText::Text::dirtyBound();
    osg::Geometry::setBound(osgText::Text::computeBound());

    if(c[3] != 1.0)
        osgText::Text::getOrCreateStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
    else
        osgText::Text::getOrCreateStateSet()->setRenderingHint(osg::StateSet::DEFAULT_BIN);

	// reset flag
    _dirty = false;
}

void TextShape::drawImplementation(osg::RenderInfo &renderInfo) const
{
    osgText::Text::drawImplementation(renderInfo);    
}
