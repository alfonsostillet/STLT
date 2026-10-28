#pragma once
#include <memory>
#include <string>
#include <vector>
#include "fx/Effect.h"
#include "fx/FfglAbi.h"

namespace stltvj
{
// A loaded FFGL 2 plugin file (.dll, .bundle or .so). Stays loaded for the life of the app,
// because instances made from it may live anywhere in a composition.
class FfglLibrary
{
public:
	static std::shared_ptr< FfglLibrary > Load( const std::string& path, std::string& error );
	~FfglLibrary();

	ffgl::Mixed Call( ffgl::UInt32 code, ffgl::Mixed input, ffgl::InstanceId instance = nullptr ) const;
	ffgl::Mixed Call( ffgl::UInt32 code, ffgl::UInt32 input, ffgl::InstanceId instance = nullptr ) const;
	ffgl::Mixed Call( ffgl::UInt32 code, void* input, ffgl::InstanceId instance = nullptr ) const;

	// Reads a parameter's description; `instance` may be null (the plugin's defaults).
	ParamInfo DescribeParam( int index, ffgl::InstanceId instance ) const;
	void ReadOptions( ParamInfo& param, int index, ffgl::InstanceId instance ) const;

	std::string path;
	std::string name;     // from the plugin info, up to 16 characters
	std::string uniqueId; // four characters
	std::string description;
	ffgl::UInt32 type = ffgl::TYPE_EFFECT;
	int apiMajor = 0, apiMinor = 0;
	bool wantsTime = false;
	int minInputs = 1;
	std::vector< ParamInfo > params;

private:
	FfglLibrary() = default;
	void* handle            = nullptr;
	ffgl::PlugMain plugMain = nullptr;
};

// One instance of an FFGL plugin in a chain. The GL instance is created on first use, at the size it renders at.
class FfglEffect : public Effect
{
public:
	explicit FfglEffect( std::shared_ptr< FfglLibrary > library );
	~FfglEffect() override;

	std::string Name() const override { return library->name; }
	std::string Id() const override;
	bool IsSource() const override { return library->type == ffgl::TYPE_SOURCE; }
	std::string Error() const override { return error; }
	bool Process( GLuint input, int width, int height, GLuint targetFbo, const FrameInfo& frame ) override;

private:
	void PushValue( int index, float value ) override;
	void PushText( int index, const std::string& text ) override;
	void PollChanges() override;
	void PushAll();

	std::shared_ptr< FfglLibrary > library;
	ffgl::InstanceId instance = nullptr;
	int instanceWidth = 0, instanceHeight = 0;
	bool failed = false;
	std::string error;
};

std::string FfglId( const FfglLibrary& library );
}
