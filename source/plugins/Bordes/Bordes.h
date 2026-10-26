#pragma once
#include <string>
#include <FFGLSDK.h>

class Bordes : public CFFGLPlugin
{
public:
	Bordes();
	~Bordes();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int dwIndex, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;

	// Todos los parametros llegan normalizados (0..1) desde Resolume.
	float grosor;
	float umbral;
	float intensidad;
	float edgeR, edgeG, edgeB;
	float bgR, bgG, bgB;
	float mezcla;
	float colorOriginal;
	float invertir;
};
