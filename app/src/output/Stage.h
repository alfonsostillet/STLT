#pragma once
#include <string>
#include <vector>

namespace stltvj
{
struct Vec2; // output/AdvancedOutput.h

// The space a show is set in, in metres, and the projectors in it where they really stand: the Advanced Output's
// third step. From it STLT works out what each projector lights (its footprint on each surface), how sharp and
// bright it is there, where projectors overlap, and the slices a projector's screen needs.
//
// Axes: x to the right, y up, z towards the audience (a façade faces +z). Angles in degrees.
struct Vec3
{
	float x = 0, y = 0, z = 0;
};

struct StageSurface
{
	enum class Kind
	{
		Plane, // a flat rectangle: a façade, a screen, a floor
		Box,   // a block: its six faces
	};
	std::string id;
	std::string name = "Surface";
	Kind kind = Kind::Plane;
	Vec3 position;                    // its centre
	Vec3 size     = { 10, 5, 1 };     // width, height (and depth, for a box)
	Vec3 rotation;                    // yaw (around y), pitch (around x), roll (around z)
};

// One flat face of a surface: corners origin, origin + ex * width, origin + ex * width + ey * height,
// origin + ey * height (ex, ey of unit length); its front is the side cross( ex, ey ) points to.
struct StageFace
{
	int surface = -1;
	int face    = 0; // 0 for a plane; 0..5 for a box (front, back, left, right, top, bottom)
	Vec3 origin, ex, ey;
	float width = 0, height = 0;
	Vec3 Normal() const;
	Vec3 Corner( int i ) const; // 0..3, as above
};

// A projector as its data sheet gives it (no model list: what matters is entered by hand).
struct StageProjector
{
	std::string id;
	std::string name = "Projector";
	Vec3 position          = { 0, 2, 20 };
	float pan = 0, tilt = 0, roll = 0; // pan 0: looking along -z; tilt up positive
	int width = 1920, height = 1200;   // its resolution
	float throwRatio = 1.65f;          // distance / picture width
	float throwMin = 1.39f, throwMax = 2.23f; // the zoom's range ("" when it has a fixed lens: min = max)
	float shiftX = 0, shiftY = 0;      // lens shift, in picture widths and heights (0.15: up by 15 % of the height)
	float lumens = 20000;
	std::string screen;                // the id of the Advanced Output screen it shows ("" none)
};

struct Stage
{
	std::vector< StageSurface > surfaces;
	std::vector< StageProjector > projectors;
	bool Empty() const { return surfaces.empty() && projectors.empty(); }
	// A façade 20 x 12 m and the ground in front of it, and no projector: where a new stage starts. Ids are given
	// by AdvancedOutputSetup::EnsureIds, with the rest of the setup's.
	static Stage MakeDefault();
};

std::vector< StageFace > Faces( const Stage& stage );

// The projector's lens: its position, its axes, and the size of its picture one metre away.
struct ProjectorFrame
{
	Vec3 position, forward, right, up;
	float halfWidth = 0, halfHeight = 0; // at one metre
	float centreX = 0, centreY = 0;      // the picture's centre at one metre, moved by the lens shift
};
ProjectorFrame FrameOf( const StageProjector& projector );
// The direction through a point of the picture: u, v from -1 to 1 (left to right, bottom to top).
Vec3 RayThrough( const ProjectorFrame& frame, float u, float v );
// Where a point falls in the projector's picture, in its pixels (y down). False when it is behind the lens.
bool ProjectToPicture( const StageProjector& projector, Vec3 point, Vec2& pixel );

// What a projector lights on one face: the lit part (in metres, on the face), and how it looks there.
struct Lighting
{
	int face = -1;              // index into Faces( stage )
	std::vector< Vec3 > polygon; // the lit part, in the world
	float area = 0;              // m²
	float share = 0;             // of the projector's picture that falls there (0..1)
	float pixelsPerMetre = 0;
	float lux = 0;
};
// Each face the projector lights (its front facing the projector), largest first.
std::vector< Lighting > LightingOf( const std::vector< StageFace >& faces, const StageProjector& projector );
// A face's corners in the projector's picture (its pixels, y down): top-left, top-right, bottom-right, bottom-left
// as the face is seen from its front. False when a corner is behind the lens.
bool FaceInPicture( const StageProjector& projector, const StageFace& face, Vec2 corners[ 4 ] );
// The part two lit polygons share on the same face (empty when they do not overlap).
std::vector< Vec3 > Overlap( const StageFace& face, const std::vector< Vec3 >& a, const std::vector< Vec3 >& b );
// The share of a face lit by at least one projector, and by two or more (sampled on a grid).
void Coverage( const StageFace& face, const std::vector< std::vector< Vec3 > >& lit, float& covered, float& overlapped );
float PolygonArea( const std::vector< Vec3 >& polygon );

// Saved with the output setup.
std::string SaveStage( const Stage& stage );
bool LoadStage( const std::string& json, Stage& stage );
}
