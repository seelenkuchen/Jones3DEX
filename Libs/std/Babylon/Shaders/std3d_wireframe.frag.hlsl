// Added: the fragment shader of std3D_DrawLineStrip and std3D_DrawPointList (std3DBabylon.cpp), a
// port of the DirectX 9 backend's default_wf_ps.hlsl (std/Win95/DX9/Shaders before 82b8f607): the
// wireframe color times the diffuse color, no texture and no fog. std3DX9.c set g_wireframeColor to
// opaque white once and never changed it, so it is a constant here. The fixed-function alpha test of
// Direct3D 9 (std3DX9.c: D3DCMP_GREATER against D3DRS_ALPHAREF) applied to these draws too.
//
// Fragment uniform block 0 (SDL GPU: space3): the block of std3d.frag.hlsl; only the alpha test is
// read.

static const float4 kWireframeColor = float4(1.0, 1.0, 1.0, 1.0);

cbuffer Std3DFragment : register(b0, space3)
{
    float4 gFogParams; // unused here
    float4 gFogColor;  // unused here
    float4 gAlphaTest; // x: D3DRS_ALPHAREF / 255; a fragment passes when its alpha is greater
};

struct Input
{
    [[vk::location(0)]] float4 diffuse : TEXCOORD0;
};

float4 main(Input input) : SV_Target0
{
    float4 color = kWireframeColor * input.diffuse;
    if ( !(color.a > gAlphaTest.x) )
    {
        discard;
    }

    return color;
}
