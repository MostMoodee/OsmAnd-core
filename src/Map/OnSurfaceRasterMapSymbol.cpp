#include "OnSurfaceRasterMapSymbol.h"

OsmAnd::OnSurfaceRasterMapSymbol::OnSurfaceRasterMapSymbol(
    const std::shared_ptr<MapSymbolsGroup>& group_)
    : RasterMapSymbol(group_)
    , direction(0.0f)
{
}

OsmAnd::OnSurfaceRasterMapSymbol::~OnSurfaceRasterMapSymbol()
{
}

float OsmAnd::OnSurfaceRasterMapSymbol::getDirection() const
{
    return direction;
}

void OsmAnd::OnSurfaceRasterMapSymbol::setDirection(const float newDirection)
{
    direction = newDirection;
}

OsmAnd::PointI OsmAnd::OnSurfaceRasterMapSymbol::getPosition31() const
{
    return position31;
}

void OsmAnd::OnSurfaceRasterMapSymbol::setPosition31(const PointI position)
{
    position31 = position;
}
