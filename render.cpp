/*
 * Fade To Black engine rewrite
 * Copyright (C) 2006-2012 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifdef USE_GLES
#include <GLES/gl.h>
#else
#include <SDL_opengl.h>
#endif
#include <math.h>
#include "render.h"
#include "texturecache.h"

#ifdef BUFFER_TEXTPOLYGONS

static int cache_ready=0;

#include <algorithm> /* to sort GLquads */
struct GLvertex_textured {
	GLfloat x,y,z;
	GLfloat u, v;
	GLuint  t;
};

struct GLquad_textured {
	GLvertex_textured vertices[6];
};

static inline void __tGLvert(GLvertex_textured *glv, Vertex *v, GLfloat *uv, GLuint t) {
		glv->x = v->x;
		glv->y = v->y;
		glv->z = v->z;

		glv->u = uv[0];
		glv->v = uv[1];

		glv->t = t;
}

static GLuint          polytext_lt = 0; //Last texture used
static GLquad_textured polytext_quads[((64 + 1) * 64) + ((64+1) *64) + (64*64)];
static GLuint          polytext_texturecache[sizeof(polytext_quads) / sizeof(polytext_quads[0])];
static uint32_t 	   polytext_c = 0;
#endif

#ifdef BUFFER_FLATPOLYGONS
struct GLvertex_flat {
	GLfloat x,y,z;
	GLubyte r, g, b, a;
};

/*
 * __fGLvert(glv, v, r,g,b,a)
 * Fills one GLvertex_flat instance
 */

#define __fGLvert(glv, v, r,g,b,a) { \
		glv.x = v.x; \
		glv.y = v.y; \
		glv.z = v.z; \
		\
		glv.r = r; \
		glv.g = g; \
		glv.b = b; \
		glv.a = a; \
}

static GLvertex_flat polyflat_vert[20480];
static int 			 polyflat_count = 0;
#endif

#if defined(BUFFER_FLATPOLYGONS) || defined(BUFFER_TEXTPOLYGONS)
#define __OFFSET_MEMBER(p, m) ((size_t*)(&((p *)0)->m))
#endif

static const bool kOverlayDisabled = false;
static const int kOverlayBufSize = 320 * 200;

struct Vertex3f {
	GLfloat x, y, z;
};

struct Vertex4f {
	GLfloat x, y, z, w;

	void normalize() {
		const GLfloat len = sqrt(x * x + y * y + z * z);
		x /= len;
		y /= len;
		z /= len;
		w /= len;
	}
};

struct Matrix4f {
	GLfloat t[16];

	void identity() {
		memset(t, 0, sizeof(t));
		for (int i = 0; i < 3; ++i) {
			t[i * 4 + i] = 1.;
		}
	}

	static void mul(const Matrix4f& a, const Matrix4f& b, Matrix4f &res) {
		for (int i = 0; i < 16; ++i) {
			const GLfloat *va = &a.t[i & 12];
			const GLfloat *vb = &b.t[i &  3];
			res.t[i] = va[0] * vb[0] + va[1] * vb[4] + va[2] * vb[8] + va[3] * vb[12];
		}
	}
};

#ifdef USE_GLES

#define glOrtho glOrthof
#define glFrustum glFrustumf

static const int kVerticesBufferSize = 1024;
static GLfloat _verticesBuffer[kVerticesBufferSize * 3];

static GLfloat *bufferVertex(const Vertex *vertices, int count) {
	assert(count <= kVerticesBufferSize);
	GLfloat *buf = _verticesBuffer;
	for (int i = 0; i < count; ++i) {
		buf[0] = vertices[i].x;
		buf[1] = vertices[i].y;
		buf[2] = vertices[i].z;
		buf += 3;
	}
	return _verticesBuffer;
}
#endif

static void emitQuad2i(int x, int y, int w, int h) {
#ifdef USE_GLES
	GLfloat vertices[] = { x, y, x + w, y, x + w, y + h, x, y + h };
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
#else
	glBegin(GL_QUADS);
		glVertex2i(x, y);
		glVertex2i(x + w, y);
		glVertex2i(x + w, y + h);
		glVertex2i(x, y + h);
	glEnd();
#endif
}

static void emitQuadTex2i(int x, int y, int w, int h, GLfloat *uv) {
#ifdef USE_GLES
	GLfloat vertices[] = { x, y, x + w, y, x + w, y + h, x, y + h };
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, uv);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
#else
	glBegin(GL_QUADS);
		glTexCoord2f(uv[0], uv[1]);
		glVertex2i(x, y);
		glTexCoord2f(uv[2], uv[3]);
		glVertex2i(x + w, y);
		glTexCoord2f(uv[4], uv[5]);
		glVertex2i(x + w, y + h);
		glTexCoord2f(uv[6], uv[7]);
		glVertex2i(x, y + h);
	glEnd();
#endif
}

static void emitQuadTex3i(const Vertex *vertices, GLfloat *uv) {
#ifdef USE_GLES
	glVertexPointer(3, GL_FLOAT, 0, bufferVertex(vertices, 4));
	glTexCoordPointer(2, GL_FLOAT, 0, uv);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
#else
	glBegin(GL_QUADS);
		glTexCoord2f(uv[0], uv[1]);
		glVertex3i(vertices[0].x, vertices[0].y, vertices[0].z);
		glTexCoord2f(uv[2], uv[3]);
		glVertex3i(vertices[1].x, vertices[1].y, vertices[1].z);
		glTexCoord2f(uv[4], uv[5]);
		glVertex3i(vertices[2].x, vertices[2].y, vertices[2].z);
		glTexCoord2f(uv[6], uv[7]);
		glVertex3i(vertices[3].x, vertices[3].y, vertices[3].z);
	glEnd();
#endif
}

#ifdef BUFFER_TEXTPOLYGONS
static void bemitQuadTex3i(const Vertex *vertices, GLfloat *uv) {
	GLvertex_textured *vertex = polytext_quads[polytext_c++].vertices;

	__tGLvert(&vertex[0], (Vertex*)&vertices[0], &uv[0], polytext_lt);
	__tGLvert(&vertex[1], (Vertex*)&vertices[1], &uv[2], polytext_lt);
	__tGLvert(&vertex[2], (Vertex*)&vertices[2], &uv[4], polytext_lt);

	__tGLvert(&vertex[3], (Vertex*)&vertices[2], &uv[4], polytext_lt);
	__tGLvert(&vertex[4], (Vertex*)&vertices[3], &uv[6], polytext_lt);
	__tGLvert(&vertex[5], (Vertex*)&vertices[0], &uv[0], polytext_lt);
}
#endif

static void emitTriTex3i(const Vertex *vertices, const GLfloat *uv) {
#ifdef USE_GLES
	glVertexPointer(3, GL_FLOAT, 0, bufferVertex(vertices, 3));
	glTexCoordPointer(2, GL_FLOAT, 0, uv);
	glDrawArrays(GL_TRIANGLES, 0, 3);
#else
	glBegin(GL_TRIANGLES);
		glTexCoord2f(uv[0], uv[1]);
		glVertex3i(vertices[0].x, vertices[0].y, vertices[0].z);
		glTexCoord2f(uv[2], uv[3]);
		glVertex3i(vertices[1].x, vertices[1].y, vertices[1].z);
		glTexCoord2f(uv[4], uv[5]);
		glVertex3i(vertices[2].x, vertices[2].y, vertices[2].z);
	glEnd();
#endif
}

#if 0
static void emitTriFan3i(const Vertex *vertices, int count) {
#ifdef USE_GLES
	glVertexPointer(3, GL_FLOAT, 0, bufferVertex(vertices, count));
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
#else
	glBegin(GL_TRIANGLE_FAN);
		for (int i = 0; i < count; ++i) {
			glVertex3i(vertices[i].x, vertices[i].y, vertices[i].z);
		}
        glEnd();
#endif
}
#endif

static void emitPoint3f(const Vertex *pos) {
#ifdef USE_GLES
	glVertexPointer(3, GL_FLOAT, 0, bufferVertex(pos, 1));
	glDrawArrays(GL_POINTS, 0, 1);
#else
	glBegin(GL_POINTS);
		glVertex3f(pos->x, pos->y, pos->z);
	glEnd();
#endif
}

TextureCache _textureCache;
static Vertex3f _cameraPos;
static GLfloat _cameraPitch;
static Vertex4f _frustum[6];

Render::Render() {
	memset(_clut, 0, sizeof(_clut));
	_overlay.buf = (uint8_t *)calloc(kOverlayBufSize, sizeof(uint8_t));
	_overlay.tex = 0;
	_overlay.hflip = false;
	_overlay.r = _overlay.g = _overlay.b = 255;
	_viewport.changed = true;
	_viewport.pw = 256;
	_viewport.ph = 256;
	_textureCache.init();
#ifdef BUFFER_FLATPOLYGONS
	glGenBuffers(1, &VBOs[0]);
	glBindBuffer(GL_ARRAY_BUFFER, VBOs[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(polyflat_vert), NULL, GL_DYNAMIC_DRAW);
#endif
#ifdef BUFFER_TEXTPOLYGONS
	glGenBuffers(1, &VBOs[1]);
	glBindBuffer(GL_ARRAY_BUFFER, VBOs[1]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(polytext_quads), NULL, GL_STATIC_DRAW);
#endif
#if defined(BUFFER_TEXTPOLYGONS) || defined(BUFFER_FLATPOLYGONS)
	glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
}

Render::~Render() {
#ifdef BUFFER_FLATPOLYGONS
	glDeleteBuffers(1, &VBOs[0]);
#endif
#ifdef BUFFER_TEXTPOLYGONS
	glDeleteBuffers(1, &VBOs[1]);
#endif
	free(_overlay.buf);
}

void Render::flushCachedTextures() {
	_textureCache.flush();
	_overlay.tex = 0;
}

void Render::resizeScreen(int w, int h) {
	glDisable(GL_LIGHTING);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_NOTEQUAL, 0.);
	_w = w;
	_h = h;
	_viewport.changed = true;
}

void Render::setCameraPos(int x, int y, int z, int shift) {
	const GLfloat div = 1 << shift;
	_cameraPos.x = x / div;
	_cameraPos.z = z / div;
	_cameraPos.y = y / div;
}

void Render::setCameraPitch(int ry) {
	_cameraPitch = ry * 360 / 1024.;
}

#ifndef BUFFER_FLATPOLYGONS
void Render::drawPolygonFlat(const Vertex *vertices, int verticesCount, int color) {
	switch (color) {
	case kFlatColorRed:
		glColor4f(1., 0., 0., .5);
		break;
	case kFlatColorGreen:
		glColor4f(0., 1., 0., .5);
		break;
	case kFlatColorYellow:
		glColor4f(1., 1., 0., .5);
		break;
	case kFlatColorBlue:
		glColor4f(0., 0., 1., .5);
		break;
	case kFlatColorShadow:
		glColor4f(0., 0., 0., .5);
		break;
	case kFlatColorLight:
		glColor4f(1., 1., 1., .2);
		break;
	default:
		if (color >= 0 && color < 256) {
			glColor4f(_pixelColorMap[0][color], _pixelColorMap[1][color], _pixelColorMap[2][color], _pixelColorMap[3][color]);
		} else {
			warning("Render::drawPolygonFlat() unhandled color %d", color);
		}
		break;
	}
	emitTriFan3i(vertices, verticesCount);
	glColor4f(1., 1., 1., 1.);
}
#else

void Render::drawPolygonFlat(const Vertex *vertices, int verticesCount, int color) {
	GLubyte r=0,g=0,b=0,a=0;
	switch (color) {
	case kFlatColorRed:
		r = 255; g = 0; b = 0; a = 127;
		break;
	case kFlatColorGreen:
		r = 0; g = 255; b = 0; a = 127;
		break;
	case kFlatColorYellow:
		r = 255; g = 255; b = 0; a = 127;
		break;
	case kFlatColorBlue:
		r = 0; g = 0; b = 255; a = 127;
		break;
	case kFlatColorShadow:
		r = 0; g = 0; b = 0; a = 127;
		break;
	case kFlatColorLight:
		r = 255; g = 255; b = 255; a = 51;
		break;
	default:
		if (color >= 0 && color < 256) {
			r = _ubpixelColorMap[0][color];
			g = _ubpixelColorMap[1][color];
			b = _ubpixelColorMap[2][color];
			a = _ubpixelColorMap[3][color];
		} else {
			warning("Render::drawPolygonFlat() unhandled color %d", color);
		}
		break;
	}

	int i;
	for (i=2; i<verticesCount;i++) {
		/*
		 * Transforms TRIANGLE_STRIP into TRIANGLES:
		 * order doesn't matter for flat polygons.
		 * a - b
		 * | \ |
		 * c - d
		 *
		 * (a - b - c - d) -> (a-b-c), (a, c, d)
		 */
		__fGLvert(polyflat_vert[polyflat_count],   vertices[0],   r,g,b,a);
		__fGLvert(polyflat_vert[polyflat_count+1], vertices[i-1], r,g,b,a);
		__fGLvert(polyflat_vert[polyflat_count+2], vertices[i],   r,g,b,a);

		polyflat_count+=3;
	}
}

void Render::flushPolygonFlat(float yGround, int shadow) {
	glEnable(GL_COLOR_ARRAY);

	glBindBuffer(GL_ARRAY_BUFFER, VBOs[0]);

	memcpy(&polyflat_vert[polyflat_count], &polyflat_vert[0], sizeof(GLvertex_flat) * polyflat_count);
	if (likely(shadow))
	{
		for (int i=polyflat_count; i< polyflat_count << 1; i++)
		{
			polyflat_vert[i].y = 127 - yGround;

			polyflat_vert[i].r = 0;
			polyflat_vert[i].g = 0;
			polyflat_vert[i].b = 0;
			polyflat_vert[i].a = 127;
		}

		polyflat_count <<= 1;
	}


	glBufferData(GL_ARRAY_BUFFER, sizeof(GLvertex_flat) * polyflat_count, &polyflat_vert[0], GL_DYNAMIC_DRAW);
	//glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLvertex_flat) * polyflat_count, &polyflat_vert[0]);

	glVertexPointer(3, GL_FLOAT, 		 sizeof(GLvertex_flat), __OFFSET_MEMBER(GLvertex_flat, x));
	glColorPointer (4, GL_UNSIGNED_BYTE, sizeof(GLvertex_flat), __OFFSET_MEMBER(GLvertex_flat, r));
	glDrawArrays(GL_TRIANGLES, 0, polyflat_count);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glDisable(GL_COLOR_ARRAY);

	polyflat_count = 0;
}
#endif

void Render::drawPolygonTexture(const Vertex *vertices, int verticesCount, int primitive, const uint8_t *texData, int texW, int texH, int16_t texKey) {
	assert(texData && texW > 0 && texH > 0);
	assert(vertices && verticesCount >= 4);
	glEnable(GL_TEXTURE_2D);
	Texture *t = _textureCache.getCachedTexture(texData, texW, texH, texKey);
	glBindTexture(GL_TEXTURE_2D, t->id);

	const GLfloat tx = t->u;
	const GLfloat ty = t->v;
	switch (primitive) {
	case 0:
	case 2:
		//
		// 1:::2
		// :   :
		// 4:::3
		//
		{
			GLfloat uv[] = { 0., 0., tx, 0., tx, ty, 0., ty };
			emitQuadTex3i(vertices, uv);
		}
		break;
	case 1:
		//
		//   1
		//  : :
		// 3:::2
		//
		{
			GLfloat uv[] = { tx / 2, 0., tx, ty, 0., ty };
			emitTriTex3i(vertices, uv);
		}
		break;
	case 3:
	case 5:
		//
		// 4:::1
		// :   :
		// 3:::2
		//
		{
			GLfloat uv[] = { tx, 0., tx, ty, 0., ty, 0., 0. };
			emitQuadTex3i(vertices, uv);
		}
		break;
	case 4:
		//
		//   3
		//  : :
		// 2:::1
		//
		{
			GLfloat uv[] = { tx, ty, 0., ty, tx / 2, 0. };
			emitTriTex3i(vertices, uv);
		}
		break;
	case 6:
	case 8:
		//
		// 3:::4
		// :   :
		// 2:::1
		//
		{
			GLfloat uv[] = { tx, ty, 0., ty, 0., 0., tx, 0. };
			emitQuadTex3i(vertices, uv);
		}
		break;
	case 7:
		//
		//   2
		//  : :
		// 1:::3
		//
		{
			GLfloat uv[] = { .0, ty, tx / 2, 0., tx, ty };
			emitTriTex3i(vertices, uv);
		}
		break;
	case 9:
	case 10:
		//
		// 2:::3
		// :   :
		// 1:::4
		//
		{
			GLfloat uv[] = { 0., 0., 0., ty, tx, ty, tx, 0. };
			emitQuadTex3i(vertices, uv);
		}
		break;
	default:
		warning("Render::drawPolygonTexture() unhandled primitive %d", primitive);
		break;
	}
	glDisable(GL_TEXTURE_2D);
}

#ifdef BUFFER_TEXTPOLYGONS
void Render::cached_drawPolygonTexture(const Vertex *vertices, int verticesCount, int primitive, const uint8_t *texData, int texW, int texH, int16_t texKey) {
	if (likely(cache_ready))
		return;

	assert(texData && texW > 0 && texH > 0);
	assert(vertices && verticesCount >= 4);
	Texture *t = _textureCache.getCachedTexture(texData, texW, texH, texKey);
	polytext_lt = t->id;

	const GLfloat tx = t->u;
	const GLfloat ty = t->v;
	switch (primitive) {
	case 0:
	case 2:
		{
			GLfloat uv[] = { 0., 0., tx, 0., tx, ty, 0., ty };
			bemitQuadTex3i(vertices, uv);
		}
		break;
	case 3:
	case 5:
		{
			GLfloat uv[] = { tx, 0., tx, ty, 0., ty, 0., 0. };
			bemitQuadTex3i(vertices, uv);
		}
		break;
	case 6:
	case 8:
		{
			GLfloat uv[] = { tx, ty, 0., ty, 0., 0., tx, 0. };
			bemitQuadTex3i(vertices, uv);
		}
		break;
	case 9:
	case 10:
		{
			GLfloat uv[] = { 0., 0., 0., ty, tx, ty, tx, 0. };
			bemitQuadTex3i(vertices, uv);
		}
		break;
	default:
		warning("Render::drawPolygonTexture() unhandled primitive %d", primitive);
		break;
	}
}

static bool comp_quads(const GLquad_textured & q1, const GLquad_textured & q2)
{
	return q1.vertices[0].t < q2.vertices[0].t ;
}

void Render::flushQuads()
{
	polytext_c = 0;
	cache_ready = 0;
}

void Render::renderQuads()
{
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ARRAY_BUFFER);
	glEnable(GL_CULL_FACE);

	glBindBuffer(GL_ARRAY_BUFFER, VBOs[1]);

	if (unlikely(!cache_ready)) {
		std::sort(&polytext_quads[0], &polytext_quads[polytext_c], comp_quads);

		glBufferData(GL_ARRAY_BUFFER, sizeof(GLquad_textured) * polytext_c, &polytext_quads[0].vertices[0], GL_DYNAMIC_DRAW);
		cache_ready = 1;
	}


	glVertexPointer  (3, GL_FLOAT, sizeof(GLvertex_textured), __OFFSET_MEMBER(GLvertex_textured,x));
	glTexCoordPointer(2, GL_FLOAT, sizeof(GLvertex_textured), __OFFSET_MEMBER(GLvertex_textured,u));

	int tail=0;
	for (int head = 1; head<polytext_c; head++) {
		//printf("%i %i\n", head, polytext_c);
		if (unlikely(polytext_quads[tail].vertices[0].t != polytext_quads[head].vertices[0].t))
		{
			glBindTexture(GL_TEXTURE_2D, polytext_quads[tail].vertices[0].t);
			glDrawArrays(GL_TRIANGLES, tail*6, (head-tail)*6);
			tail = head;
		}
		else if (head == polytext_c-1)
		{
			glBindTexture(GL_TEXTURE_2D, polytext_quads[head].vertices[0].t);
			glDrawArrays(GL_TRIANGLES, tail*6, (head-tail+1)*6);
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glDisable(GL_CULL_FACE);
	glDisable(GL_ARRAY_BUFFER);
	glDisable(GL_TEXTURE_2D);
}

#endif

void Render::drawParticle(const Vertex *pos, int color) {
	assert(color >= 0 && color < 256);
	glColor4f(_pixelColorMap[0][color], _pixelColorMap[1][color], _pixelColorMap[2][color], 1.);
	glPointSize(1.5);
	emitPoint3f(pos);
	glPointSize(1.);
	glColor4f(1., 1., 1., 1.);
}

void Render::drawSprite(int x, int y, const uint8_t *texData, int texW, int texH, int16_t texKey) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	Texture *t = _textureCache.getCachedTexture(texData, texW, texH, texKey);
	glBindTexture(GL_TEXTURE_2D, t->id);
	GLfloat uv[] = { 0., 0., t->u, 0., t->u, t->v, 0., t->v };
	emitQuadTex2i(x, y, texW, texH, uv);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
}

void Render::drawRectangle(int x, int y, int w, int h, int color) {
	glDisable(GL_DEPTH_TEST);
	assert(color >= 0 && color < 256);
	glColor4f(_pixelColorMap[0][color], _pixelColorMap[1][color], _pixelColorMap[2][color], _pixelColorMap[3][color]);
	emitQuad2i(x, y, w, h);
	glColor4f(1., 1., 1., 1.);
	glEnable(GL_DEPTH_TEST);
}

void Render::copyToOverlay(int x, int y, const uint8_t *data, int pitch, int w, int h, int transparentColor) {
	if (kOverlayDisabled) return;
	assert(_overlay.tex);
	assert(x + w <= _overlay.tex->bitmapW);
	assert(y + h <= _overlay.tex->bitmapH);
	const int dstPitch = _overlay.tex->bitmapW;
	uint8_t *dst = _overlay.buf + y * dstPitch + x;
	if (transparentColor == -1) {
		while (h--) {
			memcpy(dst, data, w);
			dst += dstPitch;
			data += pitch;
		}
	} else {
		while (h--) {
			for (int i = 0; i < w; ++i) {
				if (data[i] != transparentColor) {
					dst[i] = data[i];
				}
			}
			dst += dstPitch;
			data += pitch;
		}
	}
	_textureCache.updateTexture(_overlay.tex, _overlay.buf, _overlay.tex->bitmapW, _overlay.tex->bitmapH);
}

void Render::beginObjectDraw(int x, int y, int z, int ry, int shift) {
	glPushMatrix();
	const GLfloat div = 1 << shift;
	glTranslatef(x / div, y / div, z / div);
	glRotatef(ry * 360 / 1024., 0., 1., 0.);
	glScalef(1 / 8., 1 / 2., 1 / 8.);
}

void Render::endObjectDraw() {
	glPopMatrix();
}

void Render::updateFrustrumPlanes() {
	Matrix4f clip, proj, modl;
	glGetFloatv(GL_PROJECTION_MATRIX, proj.t);
	glGetFloatv(GL_MODELVIEW_MATRIX, modl.t);
	Matrix4f::mul(modl, proj, clip);
	// extract right,left,top,bottom,far,near planes
	const GLfloat *v = &clip.t[0];
	int i = 0;
	while (i < 6) {
		_frustum[i].x = clip.t[3]  - v[0];
		_frustum[i].y = clip.t[7]  - v[4];
		_frustum[i].z = clip.t[11] - v[8];
		_frustum[i].w = clip.t[15] - v[12];
		_frustum[i].normalize();
		++i;
		_frustum[i].x = clip.t[3]  + v[0];
		_frustum[i].y = clip.t[7]  + v[4];
		_frustum[i].z = clip.t[11] + v[8];
		_frustum[i].w = clip.t[15] + v[12];
		_frustum[i].normalize();
		++i;
		++v;
	}
}

bool Render::isQuadInFrustrum(const Vertex *vertices, int verticesCount) {
	assert(verticesCount == 4);
	bool ret = false;
	while (verticesCount-- && !ret) {
		ret = true;
		for (int i = 0; i < 6; ++i) {
			if (_frustum[i].x * vertices->x + _frustum[i].y * vertices->y + _frustum[i].z * vertices->z + _frustum[i].w <= 0) {
				ret = false;
				break;
			}
		}
		++vertices;
	}
	return ret;
}

bool Render::isBoxInFrustrum(const Vertex *vertices, int verticesCount) {
	assert(verticesCount == 8);
	for (int i = 0; i < 6; ++i) {
		bool ret = false;
		for (int j = 0; j < verticesCount; ++j) {
			if (_frustum[i].x * vertices[j].x + _frustum[i].y * vertices[j].y + _frustum[i].z * vertices[j].z + _frustum[i].w > 0) {
				ret = true;
				break;
			}
		}
		if (!ret) {
			return false;
		}
	}
	return true;
}

void Render::setOverlayBlendColor(int r, int g, int b) {
	_overlay.r = r;
	_overlay.g = g;
	_overlay.b = b;
}

void Render::setOverlayDim(int w, int h, bool hflip) {
	if (_overlay.tex) {
		_textureCache.destroyTexture(_overlay.tex);
		_overlay.tex = 0;
	}
	if (w == 0 && h == 0) {
		return;
	}
	memset(_overlay.buf, 0, kOverlayBufSize);
	_overlay.tex = _textureCache.createTexture(_overlay.buf, w, h);
	_overlay.hflip = hflip;
}

void Render::setPalette(const uint8_t *pal, int count, bool updateTextures) {
	for (int i = 0; i < count; ++i) {
		const int r = pal[0];
		const int g = pal[1];
		const int b = pal[2];
		_clut[3 * i] = r;
		_clut[3 * i + 1] = g;
		_clut[3 * i + 2] = b;
		_pixelColorMap[0][i] = r / 255.;
		_pixelColorMap[1][i] = g / 255.;
		_pixelColorMap[2][i] = b / 255.;
		_pixelColorMap[3][i] = (i == 0) ? 0. : 1.;

		_ubpixelColorMap[0][i] = r;
		_ubpixelColorMap[1][i] = g;
		_ubpixelColorMap[2][i] = b;
		_ubpixelColorMap[3][i] = (i == 0) ? 0 : 255;
		pal += 3;
	}
	_textureCache.setPalette(_clut, updateTextures);
}

void Render::clearScreen() {
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void setPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar) {
	const GLfloat y = znear * tan(fovy * M_PI / 360.);
	const GLfloat x = y * aspect;
	glFrustum(-x, x, -y, y, znear, zfar);
}

void Render::setupProjection(int mode) {
#ifdef USE_GLES
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
	if (mode == 1) {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		setPerspective(45., 1.6, 1., 128.);
		glTranslatef(0., 0., -24.);
		glRotatef(20., 1., 0., 0.);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glScalef(1., -.5, 1.);
		glTranslatef(0., 0., -64.);
		return;
	}
	clearScreen();
	if (_viewport.changed) {
		_viewport.changed = false;
		const int w = _w * _viewport.pw >> 8;
		const int h = _h * _viewport.ph >> 8;
		glViewport((_w - w) / 2, (_h - h) / 2, w, h);
	}
	if (mode == 2) {
		return;
	}
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	setPerspective(45., 1.6, 1., 512.);
	glTranslatef(0., 0., -24.);
	glRotatef(20., 1., 0., 0.);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glScalef(1., -.5, -1.);
	glRotatef(_cameraPitch, 0., 1., 0.);
	_cameraPos.y = -24;
	glTranslatef(-_cameraPos.x, _cameraPos.y, -_cameraPos.z);
	updateFrustrumPlanes();
}

void Render::setupProjection2d() {
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 320, 200, 0, 0, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void Render::drawOverlay() {
	if (!kOverlayDisabled && _overlay.tex) {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		if (_overlay.hflip) {
			glOrtho(0, _w, 0, _h, 0, 1);
		} else {
			glOrtho(0, _w, _h, 0, 0, 1);
			memset(_overlay.buf, 0, kOverlayBufSize);
		}
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, _overlay.tex->id);
		const GLfloat tU = _overlay.tex->u;
		const GLfloat tV = _overlay.tex->v;
		assert(tU != 0. && tV != 0.);
		GLfloat uv[] = { 0., 0., tU, 0., tU, tV, 0., tV };
		emitQuadTex2i(0, 0, _w, _h, uv);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_TEXTURE_2D);
	}
	if (_overlay.r != 255 || _overlay.g != 255 || _overlay.b != 255) {
		glColor4f(_overlay.r / 255., _overlay.g / 255., _overlay.b / 255., .8);
		emitQuad2i(0, 0, _w, _h);
		glColor4f(1., 1., 1., 1.);
		_overlay.r = _overlay.g = _overlay.b = 255;
	}
}
