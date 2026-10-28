#pragma once
// The FFGL 2 binary interface as a host sees it: function codes, constants and the structs passed
// through plugMain. Values and layouts follow the FFGL 2.3 specification (resolume/ffgl, FFGL.h);
// declared here so the host does not need the SDK's OpenGL headers.
#include <cstdint>

namespace stltvj::ffgl
{
using UInt32 = uint32_t;
using UInt64 = uint64_t;

union Mixed
{
	UInt32 UIntValue;
	void* PointerValue;
};
using InstanceId = void*;

#if defined( _WIN32 )
typedef Mixed( __stdcall* PlugMain )( UInt32, Mixed, InstanceId );
#else
typedef Mixed ( *PlugMain )( UInt32, Mixed, InstanceId );
#endif

constexpr UInt32 GET_INFO                     = 0;
constexpr UInt32 INITIALISE_V2                = 34;
constexpr UInt32 DEINITIALISE                 = 2;
constexpr UInt32 GET_NUM_PARAMETERS           = 4;
constexpr UInt32 GET_PARAMETER_NAME           = 5;
constexpr UInt32 GET_PARAMETER_DEFAULT        = 6;
constexpr UInt32 GET_PARAMETER_DISPLAY        = 7;
constexpr UInt32 SET_PARAMETER                = 8;
constexpr UInt32 GET_PARAMETER                = 9;
constexpr UInt32 GET_PLUGIN_CAPS              = 10;
constexpr UInt32 GET_EXTENDED_INFO            = 13;
constexpr UInt32 GET_PARAMETER_TYPE           = 15;
constexpr UInt32 PROCESS_OPENGL               = 17;
constexpr UInt32 INSTANTIATE_GL               = 18;
constexpr UInt32 DEINSTANTIATE_GL             = 19;
constexpr UInt32 SET_TIME                     = 20;
constexpr UInt32 RESIZE                       = 23;
constexpr UInt32 GET_NUM_PARAMETER_ELEMENTS   = 31;
constexpr UInt32 GET_PARAMETER_ELEMENT_NAME   = 35;
constexpr UInt32 GET_PARAMETER_ELEMENT_VALUE  = 36;
constexpr UInt32 SET_BEATINFO                 = 38;
constexpr UInt32 SET_HOSTINFO                 = 39;
constexpr UInt32 GET_RANGE                    = 41;
constexpr UInt32 GET_PARAMETER_VISIBILITY     = 45;
constexpr UInt32 GET_PARAMETER_EVENTS         = 46;
constexpr UInt32 GET_PARAM_GROUP              = 50;
constexpr UInt32 GET_PARAM_DISPLAY_NAME       = 51;

constexpr UInt32 SUCCESS = 0;
constexpr UInt32 FAIL    = 0xFFFFFFFF;

constexpr UInt32 TYPE_EFFECT = 0;
constexpr UInt32 TYPE_SOURCE = 1;
constexpr UInt32 TYPE_MIXER  = 2;

constexpr UInt32 CAP_SET_TIME             = 5;
constexpr UInt32 CAP_MINIMUM_INPUT_FRAMES = 10;
constexpr UInt32 CAP_MAXIMUM_INPUT_FRAMES = 11;

constexpr UInt32 PARAM_BOOLEAN    = 0;
constexpr UInt32 PARAM_EVENT      = 1;
constexpr UInt32 PARAM_RED        = 2;
constexpr UInt32 PARAM_GREEN      = 3;
constexpr UInt32 PARAM_BLUE       = 4;
constexpr UInt32 PARAM_XPOS       = 5;
constexpr UInt32 PARAM_YPOS       = 6;
constexpr UInt32 PARAM_STANDARD   = 10;
constexpr UInt32 PARAM_OPTION     = 11;
constexpr UInt32 PARAM_BUFFER     = 12;
constexpr UInt32 PARAM_INTEGER    = 13;
constexpr UInt32 PARAM_FILE       = 14;
constexpr UInt32 PARAM_TEXT       = 100;
constexpr UInt32 PARAM_HUE        = 200;
constexpr UInt32 PARAM_SATURATION = 201;
constexpr UInt32 PARAM_BRIGHTNESS = 202;
constexpr UInt32 PARAM_ALPHA      = 203;

constexpr UInt64 EVENT_VISIBILITY   = 0x01;
constexpr UInt64 EVENT_DISPLAY_NAME = 0x02;
constexpr UInt64 EVENT_VALUE        = 0x04;
constexpr UInt64 EVENT_ELEMENTS     = 0x08;

struct PluginInfo
{
	UInt32 apiMajorVersion;
	UInt32 apiMinorVersion;
	char uniqueId[ 4 ];
	char name[ 16 ];
	UInt32 type;
};

struct ExtendedInfo
{
	UInt32 majorVersion;
	UInt32 minorVersion;
	const char* description;
	const char* about;
	UInt32 extendedDataSize;
	void* extendedData;
};

struct SetParameter
{
	UInt32 index;
	Mixed value;
};

struct BeatInfo
{
	float bpm;
	float barPhase;
};

struct HostInfo
{
	const char* name;
	const char* version;
};

struct GetRange
{
	UInt32 index;
	float min, max;
};

struct GetString
{
	UInt32 index;
	char* address;
	UInt32 maxToWrite; // plugins write no terminating nul
};

struct Viewport
{
	uint32_t x, y, width, height; // GLuint
};

struct Texture
{
	UInt32 width, height;
	UInt32 hardwareWidth, hardwareHeight;
	uint32_t handle; // GLuint
};

struct ProcessOpenGL
{
	UInt32 inputCount;
	Texture** inputs;
	uint32_t hostFbo; // GLuint
};

struct ElementQuery
{
	UInt32 index;
	UInt32 element;
};

struct ParamEvent
{
	UInt32 index;
	UInt64 flags;
};

struct GetParamEvents
{
	UInt32 count;
	ParamEvent* events;
};
}
