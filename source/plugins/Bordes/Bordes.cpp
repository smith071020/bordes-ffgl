#include "Bordes.h"
using namespace ffglex;

enum ParamType : FFUInt32
{
	PT_GROSOR,
	PT_UMBRAL,
	PT_INTENSIDAD,
	PT_EDGE_R,
	PT_EDGE_G,
	PT_EDGE_B,
	PT_BG_R,
	PT_BG_G,
	PT_BG_B,
	PT_MEZCLA,
	PT_COLOR_ORIGINAL,
	PT_INVERTIR
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Bordes >,                 // Create method
	"ZBRD",                                  // Plugin unique ID of maximum length 4.
	"Bordes",                                // Plugin name
	2,                                       // API major version number
	1,                                       // API minor version number
	1,                                       // Plugin major version number
	0,                                       // Plugin minor version number
	FF_EFFECT,                               // Plugin type
	"Deteccion de bordes (Sobel)",           // Plugin description
	"Bordes - efecto de deteccion de bordes" // About
);

static const char _vertexShaderCode[] = R"(#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
)";

static const char _fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform vec2 TexelSize;
uniform float Grosor;
uniform float Umbral;
uniform float Intensidad;
uniform vec3 ColorBorde;
uniform vec3 ColorFondo;
uniform float Mezcla;
uniform float ColorOriginal;
uniform float Invertir;

in vec2 uv;

out vec4 fragColor;

// Devuelve el color sin premultiplicar (la textura de entrada viene premultiplicada).
vec4 muestraColor( vec2 p )
{
	p = clamp( p, vec2( 0.0 ), MaxUV - TexelSize * 0.5 );
	vec4 c = texture( InputTexture, p );
	if( c.a > 0.0 )
		c.rgb /= c.a;
	return c;
}

float muestra( vec2 p )
{
	return dot( muestraColor( p ).rgb, vec3( 0.299, 0.587, 0.114 ) );
}

void main()
{
	vec2 px = TexelSize * Grosor;

	float tl = muestra( uv + vec2( -px.x,  px.y ) );
	float t  = muestra( uv + vec2(  0.0,   px.y ) );
	float tr = muestra( uv + vec2(  px.x,  px.y ) );
	float l  = muestra( uv + vec2( -px.x,  0.0  ) );
	float r  = muestra( uv + vec2(  px.x,  0.0  ) );
	float bl = muestra( uv + vec2( -px.x, -px.y ) );
	float b  = muestra( uv + vec2(  0.0,  -px.y ) );
	float br = muestra( uv + vec2(  px.x, -px.y ) );

	float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
	float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;

	float borde = length( vec2( gx, gy ) ) * Intensidad;
	borde = smoothstep( Umbral, Umbral + 0.1, borde );
	if( Invertir > 0.5 )
		borde = 1.0 - borde;

	vec4 original = muestraColor( uv );
	vec3 cBorde = ( ColorOriginal > 0.5 ) ? original.rgb : ColorBorde;

	vec3 resultado = mix( ColorFondo, cBorde, borde );
	resultado = mix( resultado, original.rgb, Mezcla );

	// El plugin debe devolver colores premultiplicados.
	resultado = clamp( resultado * original.a, vec3( 0.0 ), vec3( original.a ) );
	fragColor = vec4( resultado, original.a );
}
)";

Bordes::Bordes() :
	// Valores por defecto normalizados (0..1).
	grosor( 0.0909f ),// 1.0 px  (0.5 + v * 5.5)
	umbral( 0.1f ),
	intensidad( 0.2f ),// 2.0   (v * 10)
	edgeR( 1.0f ), edgeG( 1.0f ), edgeB( 1.0f ),
	bgR( 0.0f ), bgG( 0.0f ), bgB( 0.0f ),
	mezcla( 0.0f ),
	colorOriginal( 0.0f ),
	invertir( 0.0f )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	SetParamInfof( PT_GROSOR, "Grosor", FF_TYPE_STANDARD );
	SetParamInfof( PT_UMBRAL, "Umbral", FF_TYPE_STANDARD );
	SetParamInfof( PT_INTENSIDAD, "Intensidad", FF_TYPE_STANDARD );
	SetParamInfof( PT_EDGE_R, "Color borde", FF_TYPE_RED );
	SetParamInfof( PT_EDGE_G, "Color borde G", FF_TYPE_GREEN );
	SetParamInfof( PT_EDGE_B, "Color borde B", FF_TYPE_BLUE );
	SetParamInfof( PT_BG_R, "Color fondo", FF_TYPE_RED );
	SetParamInfof( PT_BG_G, "Color fondo G", FF_TYPE_GREEN );
	SetParamInfof( PT_BG_B, "Color fondo B", FF_TYPE_BLUE );
	SetParamInfof( PT_MEZCLA, "Mezcla original", FF_TYPE_STANDARD );
	SetParamInfof( PT_COLOR_ORIGINAL, "Color original", FF_TYPE_BOOLEAN );
	SetParamInfof( PT_INVERTIR, "Invertir", FF_TYPE_BOOLEAN );

	FFGLLog::LogToHost( "Created Bordes effect" );
}
Bordes::~Bordes()
{
}

FFResult Bordes::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( _vertexShaderCode, _fragmentShaderCode ) )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	//Use base-class init as success result so that it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}
FFResult Bordes::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 )
		return FF_FAIL;

	if( pGL->inputTextures[ 0 ] == NULL )
		return FF_FAIL;

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( pGL->inputTextures[ 0 ]->Handle );

	shader.Set( "InputTexture", 0 );

	FFGLTexCoords maxCoords = GetMaxGLTexCoords( *pGL->inputTextures[ 0 ] );
	shader.Set( "MaxUV", maxCoords.s, maxCoords.t );

	// Un pixel equivale a 1 / tamano real de la textura en coordenadas de textura.
	float hw = (float)pGL->inputTextures[ 0 ]->HardwareWidth;
	float hh = (float)pGL->inputTextures[ 0 ]->HardwareHeight;
	if( hw < 1.0f )
		hw = 1.0f;
	if( hh < 1.0f )
		hh = 1.0f;
	shader.Set( "TexelSize", 1.0f / hw, 1.0f / hh );

	// Mapeo de los parametros normalizados a sus rangos reales.
	shader.Set( "Grosor", 0.5f + grosor * 5.5f );     // 0.5 .. 6.0
	shader.Set( "Umbral", umbral );                    // 0 .. 1
	shader.Set( "Intensidad", intensidad * 10.0f );    // 0 .. 10
	glUniform3f( shader.FindUniform( "ColorBorde" ), edgeR, edgeG, edgeB );
	glUniform3f( shader.FindUniform( "ColorFondo" ), bgR, bgG, bgB );
	shader.Set( "Mezcla", mezcla );
	shader.Set( "ColorOriginal", colorOriginal );
	shader.Set( "Invertir", invertir );

	quad.Draw();

	return FF_SUCCESS;
}
FFResult Bordes::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();

	return FF_SUCCESS;
}

FFResult Bordes::SetFloatParameter( unsigned int dwIndex, float value )
{
	switch( dwIndex )
	{
	case PT_GROSOR:         grosor = value; break;
	case PT_UMBRAL:         umbral = value; break;
	case PT_INTENSIDAD:     intensidad = value; break;
	case PT_EDGE_R:         edgeR = value; break;
	case PT_EDGE_G:         edgeG = value; break;
	case PT_EDGE_B:         edgeB = value; break;
	case PT_BG_R:           bgR = value; break;
	case PT_BG_G:           bgG = value; break;
	case PT_BG_B:           bgB = value; break;
	case PT_MEZCLA:         mezcla = value; break;
	case PT_COLOR_ORIGINAL: colorOriginal = value; break;
	case PT_INVERTIR:       invertir = value; break;
	default:
		return FF_FAIL;
	}

	return FF_SUCCESS;
}

float Bordes::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_GROSOR:         return grosor;
	case PT_UMBRAL:         return umbral;
	case PT_INTENSIDAD:     return intensidad;
	case PT_EDGE_R:         return edgeR;
	case PT_EDGE_G:         return edgeG;
	case PT_EDGE_B:         return edgeB;
	case PT_BG_R:           return bgR;
	case PT_BG_G:           return bgG;
	case PT_BG_B:           return bgB;
	case PT_MEZCLA:         return mezcla;
	case PT_COLOR_ORIGINAL: return colorOriginal;
	case PT_INVERTIR:       return invertir;
	}

	return 0.0f;
}
