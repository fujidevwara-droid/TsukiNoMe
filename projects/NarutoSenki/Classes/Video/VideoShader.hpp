#pragma once
#include "IVideoBackend.h"
#include "Defines.h"

// The shaders CCVideoSprite draws with.
//
// Both programs use the engine's own vertex shader (position, colour, texcoord), so
// the quad, its transform and the sprite's colour tint behave exactly as for any
// CCSprite. The fragment shader differs from the engine's in two ways:
//
//  - opacity comes from the u_opacity uniform (the sprite's displayed opacity, so
//    parent opacity cascades) instead of from the vertex colour's alpha;
//  - u_premultiply is 1 when the sprite's blend function is the premultiplied one
//    (source GL_ONE): the colour is then multiplied by the opacity.
//
// The OES program is for Android's SurfaceTexture (GL_TEXTURE_EXTERNAL_OES). A
// SurfaceTexture hands out its picture with a transform (u_texMatrix) that has to be
// applied to the texture coordinate, and its (0,0) is the bottom left, whereas the
// quad's (0,0) is the top left, hence the 1.0 - y.
namespace VideoShader
{
	inline const char *programKey(VideoTextureKind kind)
	{
		return kind == VideoTextureKind::ExternalOES ? "NS_ShaderVideoOES" : "NS_ShaderVideo2D";
	}

	inline string fragmentSource(VideoTextureKind kind)
	{
		const bool oes = kind == VideoTextureKind::ExternalOES;

		// "#extension" has to come before anything but preprocessor lines.
		string source;
		if (oes)
			source += "#extension GL_OES_EGL_image_external : require\n";

		source +=
			"#ifdef GL_ES\n"
			"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
			"precision highp float;\n"
			"#else\n"
			"precision mediump float;\n"
			"#endif\n"
			"#endif\n"
			"varying vec2 v_texCoord;\n"
			"varying vec4 v_fragmentColor;\n"
			"uniform float u_opacity;\n"
			"uniform float u_premultiply;\n";

		if (oes)
		{
			source +=
				"uniform samplerExternalOES CC_Texture0;\n"
				"uniform mat4 u_texMatrix;\n"
				"void main()\n"
				"{\n"
				"    vec2 st = (u_texMatrix * vec4(v_texCoord.x, 1.0 - v_texCoord.y, 0.0, 1.0)).xy;\n"
				"    vec3 rgb = texture2D(CC_Texture0, st).rgb * v_fragmentColor.rgb;\n";
		}
		else
		{
			source +=
				"uniform sampler2D CC_Texture0;\n"
				"void main()\n"
				"{\n"
				"    vec3 rgb = texture2D(CC_Texture0, v_texCoord).rgb * v_fragmentColor.rgb;\n";
		}

		source +=
			"    float a = u_opacity * v_fragmentColor.a;\n"
			"    gl_FragColor = vec4(mix(rgb, rgb * a, u_premultiply), a);\n"
			"}\n";
		return source;
	}

	// Built on first use and kept in the shader cache. It is looked up every time, so
	// a purged cache or a rebuilt GL context just makes the next call compile it again.
	// Returns nullptr when the shader does not compile (e.g. no OES extension).
	inline CCGLProgram *program(VideoTextureKind kind)
	{
		auto cache = CCShaderCache::sharedShaderCache();
		if (auto existing = cache->programForKey(programKey(kind)))
			return existing;

		const string source = fragmentSource(kind);
		auto created = new CCGLProgram();
		if (!created->initWithVertexShaderByteArray(ccPositionTextureColor_vert, source.c_str()))
		{
			created->release();
			return nullptr;
		}

		created->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
		created->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
		created->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
		if (!created->link())
		{
			created->release();
			return nullptr;
		}
		created->updateUniforms();

		cache->addProgram(created, programKey(kind)); // the cache keeps its own reference
		created->release();
		return created;
	}

	// After the GL context was recreated the cached programs still hold the old
	// context's ids (reloadDefaultShaders only knows the engine's own programs), so they
	// are compiled again in place; sprites keep pointing at the same objects.
	inline void rebuild()
	{
		auto cache = CCShaderCache::sharedShaderCache();
		for (VideoTextureKind kind : { VideoTextureKind::Texture2D, VideoTextureKind::ExternalOES })
		{
			auto existing = cache->programForKey(programKey(kind));
			if (!existing)
				continue;

			existing->reset();
			const string source = fragmentSource(kind);
			if (!existing->initWithVertexShaderByteArray(ccPositionTextureColor_vert, source.c_str()))
				continue;

			existing->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
			existing->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
			existing->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
			if (existing->link())
				existing->updateUniforms();
		}
	}
}
