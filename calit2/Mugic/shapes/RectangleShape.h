#ifndef _RECTANGLESHAPE_
#define _RECTANGLESHAPE_

#include "GeometryShape.h"

#include <osg/Geometry>

class RectangleShape : public GeometryShape
{
public:        

    RectangleShape(std::string command = "", std::string name = "");
	virtual ~RectangleShape();
    void update(std::string);

protected:
    RectangleShape();
    void setPosition(osg::Vec3, float width, float height);
    void setColor(osg::Vec4);
    void update();
};

#endif
