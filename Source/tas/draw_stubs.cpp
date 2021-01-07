#include <algorithm>
#include <map>

#include "cpp_quakedef.hpp"

#include "draw.hpp"

#include "strafing.hpp"
#include "utils.hpp"

void AddCurve(std::vector<PathPoint>* points, int id)
{
}

void AddRectangle(Rect rect)
{
}

void RemoveRectangles(int id)
{
}

void RemoveCurve(int id)
{
}

void Draw_Elements()
{
}

PathPoint::PathPoint()
{
	for (int i = 0; i < 3; ++i)
		point[i] = 0;
	for (int i = 0; i < 4; ++i)
		color[i] = 0;
}

Rect::Rect()
{
	for (int i = 0; i < 3; ++i)
		mins[i] = 0;
	for (int i = 0; i < 3; ++i)
		maxs[i] = 0;
	for (int i = 0; i < 4; ++i)
		color[i] = 0;
}

Rect::Rect(const std::array<float, 4>& _color, vec3_t _mins, vec3_t _maxs, int _id)
{
	VectorCopy(_mins, mins);
	VectorCopy(_maxs, maxs);
	color = _color;
	id = _id;
}

Rect Rect::Get_Rect(const std::array<float, 4>& _color, vec3_t center, float width, float height, int id)
{
	Rect r;
	r.color = _color;
	r.id = id;
	VectorCopy(center, r.mins);
	r.mins[0] -= width;
	r.mins[1] -= width;
	r.mins[2] -= height;

	VectorCopy(center, r.maxs);
	r.maxs[0] += width;
	r.maxs[1] += width;
	r.maxs[2] += height;

	return r;
}

Rect Rect::Get_Rect(const std::array<float, 4>& _color, vec3_t center, vec3_t angles, float length, float width, float height, int id)
{
	vec3_t fwd, right, up;
	vec3_t corner1, corner2;
	AngleVectors(angles, fwd, right, up);
	Rect r;
	r.color = _color;
	r.id = id;

	VectorCopy(center, corner1);
	VectorCopy(center, corner2);
	VectorScaledAdd(corner1, fwd, length / 2, corner1);
	VectorScaledAdd(corner2, fwd, -length / 2, corner2);
	VectorScaledAdd(corner1, right, width / 2, corner1);
	VectorScaledAdd(corner2, right, -width / 2, corner2);
	VectorScaledAdd(corner1, up, height / 2, corner1);
	VectorScaledAdd(corner2, up, -height / 2, corner2);

	r.mins[0] = TAS_MIN(corner1[0], corner2[0]);
	r.mins[1] = TAS_MIN(corner1[1], corner2[1]);
	r.mins[2] = TAS_MIN(corner1[2], corner2[2]);

	r.maxs[0] = TAS_MAX(corner1[0], corner2[0]);
	r.maxs[1] = TAS_MAX(corner1[1], corner2[1]);
	r.maxs[2] = TAS_MAX(corner1[2], corner2[2]);

	return r;
}
