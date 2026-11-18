#pragma once
#include "Defines.h"

// The shader of the spiral transition (see Systems/ScreenSwirl.hpp). It is put on the
// sprite of a RenderTexture that holds the picture of the world: it samples that picture
// with the pixels turned around a centre point, more the nearer they are to it (a swirl),
// from further out as the effect grows (so the picture sinks into the middle), and dims the
// result to black.
//
// The texture's coordinates are the screen's: x from 0 at the left to 1 at the right, y from
// 0 at the bottom to 1 at the top (a RenderTexture's sprite is flipped to make it so).
namespace SwirlShader
{
	inline const char *kProgramKey = "NS_ShaderSwirl";

	inline string fragmentSource()
	{
		return string(
			"#ifdef GL_ES\n"
			"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
			"precision highp float;\n"
			"#else\n"
			"precision mediump float;\n"
			"#endif\n"
			"#endif\n"
			"varying vec2 v_texCoord;\n"
			"varying vec4 v_fragmentColor;\n"
			"uniform sampler2D CC_Texture0;\n"
			"uniform vec4 u_params;\n" // x: twist (radians at the centre), y: pull, z: darkness 0-1, w: width / height
			"uniform vec2 u_center;\n" // the swirl's centre, in texture coordinates
			"void main()\n"
			"{\n"
			"    vec2 d = v_texCoord - u_center;\n"
			"    d.x *= u_params.w;\n"                      // a circle, not an ellipse
			"    float k = max(0.0, 1.0 - length(d) / 0.9);\n" // 1 at the centre, 0 at the edge of its reach
			"    k = k * k;\n"
			"    float a = u_params.x * k;\n"
			"    float s = sin(a);\n"
			"    float c = cos(a);\n"
			"    d = vec2(c * d.x - s * d.y, s * d.x + c * d.y);\n"
			"    d *= 1.0 + u_params.y * k;\n"              // sample from further out: the picture shrinks inwards
			"    d.x /= u_params.w;\n"
			"    vec2 uv = u_center + d;\n"
			"    vec4 color = texture2D(CC_Texture0, uv);\n"
			"    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)\n"
			"        color = vec4(0.0, 0.0, 0.0, 1.0);\n"    // beyond the picture: black
			"    gl_FragColor = vec4(color.rgb * (1.0 - u_params.z), 1.0);\n"
			"}\n");
	}

	// Built on first use and kept in the shader cache (looked up each time, so a cache purge
	// just makes the next call compile it again).
	inline CCGLProgram *program()
	{
		auto cache = CCShaderCache::sharedShaderCache();
		if (auto existing = cache->programForKey(kProgramKey))
			return existing;

		const string source = fragmentSource();
		auto created = new CCGLProgram();
		if (!created->initWithVertexShaderByteArray(ccPositionTextureColor_vert, source.c_str()))
		{
			created->release();
			return nullptr;
		}

		created->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
		created->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
		created->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
		created->link();
		created->updateUniforms();

		cache->addProgram(created, kProgramKey); // the cache keeps its own reference
		created->release();
		return created;
	}

	inline void setParams(float twist, float pull, float darkness, float aspect, float centerX, float centerY)
	{
		auto prog = program();
		if (!prog)
			return;

		prog->use();
		prog->setUniformLocationWith4f(prog->getUniformLocationForName("u_params"), twist, pull, darkness, aspect);
		prog->setUniformLocationWith2f(prog->getUniformLocationForName("u_center"), centerX, centerY);
	}
}
