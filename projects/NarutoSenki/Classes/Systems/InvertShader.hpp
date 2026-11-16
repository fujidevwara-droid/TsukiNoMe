#pragma once
#include "Defines.h"
#include "Effect.h"

// The shader half of the colour inversion (see Systems/ScreenInvert.hpp).
//
// "Invert everything" is done with a blend-mode overlay (GameLayer::applyInvert):
// a white layer drawn with glBlendFunc(ONE_MINUS_DST_COLOR, ...) turns whatever is
// already on screen into its negative, and sits below the HUD so the HUD stays as it
// is. Inverting only one hero cannot be done that way, because the overlay does not
// know the shape of the hero, so a hero is inverted by its own shader. Mode 1
// ("everything but the hero") uses both: the overlay inverts everything and the hero's
// shader inverts the hero beforehand, so the two cancel out and it comes out
// unchanged.
//
// A circle restricts the shader to the pixels inside it. It tests the pixel's place
// on screen (gl_FragCoord) against a centre and radius that GameLayer supplies every
// frame, so it matches the circle drawn by the overlay.
//
// Sprites that are children of a SpriteBatchNode (effects, bullets, shadows) are
// drawn by the batch's own program and cannot be given this one, so they are not
// handled: with mode 1 they come out inverted along with everything else.
namespace InvertShader
{
	inline const char *programKey(bool premultiplied)
	{
		return premultiplied ? "NS_ShaderInvertPremultiplied" : "NS_ShaderInvert";
	}

	inline string fragmentSource(bool premultiplied)
	{
		// Textures with premultiplied alpha store rgb already multiplied by alpha, so
		// their negative is alpha - rgb; plain ones are 1 - rgb. The vertex colour
		// (opacity and tint) is applied afterwards, as in the default program.
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
			"uniform vec4 u_circle;\n"  // x, y: centre in framebuffer pixels, z: radius, w: 1 if a circle is used
			"uniform vec2 u_scale;\n"   // framebuffer pixels per design unit, x and y
			"void main()\n"
			"{\n"
			"    vec4 texel = texture2D(CC_Texture0, v_texCoord);\n"
			"    bool inside = true;\n"
			"    if (u_circle.w > 0.5)\n"
			"    {\n"
			"        vec2 d = (gl_FragCoord.xy - u_circle.xy) / u_scale;\n"
			"        inside = dot(d, d) < u_circle.z * u_circle.z;\n"
			"    }\n"
			"    if (inside)\n") +
			(premultiplied ? "        texel.rgb = vec3(texel.a) - texel.rgb;\n"
						   : "        texel.rgb = vec3(1.0) - texel.rgb;\n") +
			"    gl_FragColor = texel * v_fragmentColor;\n"
			"}\n";
	}

	// Built on first use and kept in the shader cache (looked up each time, so a
	// cache purge or a rebuilt GL context just makes the next call compile it again).
	inline CCGLProgram *program(bool premultiplied)
	{
		auto cache = CCShaderCache::sharedShaderCache();
		if (auto existing = cache->programForKey(programKey(premultiplied)))
			return existing;

		const string source = fragmentSource(premultiplied);
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

		cache->addProgram(created, programKey(premultiplied)); // the cache keeps its own reference
		created->release();
		return created;
	}

	// The circle for both programs: centre and radius (design units) and where the
	// centre is in framebuffer pixels. Pass useCircle = false for "everywhere".
	inline void setCircle(bool useCircle, float centerPxX, float centerPxY, float radius, float scaleX, float scaleY)
	{
		for (bool premultiplied : { false, true })
		{
			auto prog = program(premultiplied);
			if (!prog)
				continue;

			prog->use();
			prog->setUniformLocationWith4f(prog->getUniformLocationForName("u_circle"),
				centerPxX, centerPxY, radius, useCircle ? 1.0f : 0.0f);
			prog->setUniformLocationWith2f(prog->getUniformLocationForName("u_scale"), scaleX, scaleY);
		}
	}

	// Gives the node and every sprite below it the inverting program (call every
	// frame: effects the hero creates meanwhile are picked up).
	// With skipEffects the Effect sprites below the node are left as they are (mode 4
	// inverts effects but not the characters they are attached to).
	inline void apply(CCNode *node, bool skipEffects = false)
	{
		if (!node)
			return;

		if (skipEffects && dynamic_cast<Effect *>(node))
			return;

		if (auto sprite = dynamic_cast<CCSprite *>(node))
		{
			auto parent = node->getParent();
			if (sprite->getTexture() && !dynamic_cast<CCSpriteBatchNode *>(parent))
			{
				if (auto prog = program(sprite->getTexture()->hasPremultipliedAlpha()))
					sprite->setShaderProgram(prog);
			}
		}

		CCObject *child = nullptr;
		CCARRAY_FOREACH(node->getChildren(), child)
		{
			apply(static_cast<CCNode *>(child), skipEffects);
		}
	}

	// Puts the normal program back on the node and everything below it.
	inline void restore(CCNode *node)
	{
		if (!node)
			return;

		if (dynamic_cast<CCSprite *>(node))
		{
			auto parent = node->getParent();
			if (!dynamic_cast<CCSpriteBatchNode *>(parent))
			{
				if (auto normal = CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor))
					node->setShaderProgram(normal);
			}
		}

		CCObject *child = nullptr;
		CCARRAY_FOREACH(node->getChildren(), child)
		{
			restore(static_cast<CCNode *>(child));
		}
	}
}
