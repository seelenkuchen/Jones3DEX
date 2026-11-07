// Added: the vertex shader of every std3D draw (std3DBabylon.cpp), a port of the DirectX 9 backend's
// default_vs.hlsl with ScreenToClip from common.hlsli (std/Win95/DX9/Shaders before 82b8f607).
//
// The vertices are StdVertexTL, already transformed: screen x and y in pixels, z in 0..1 and rhw =
// 1 / w. Like DX9 the shader rebuilds the clip-space position with w = 1 / rhw, so the GPU
// interpolates perspective-correctly and divides back to the same screen position and z (z in 0..1
// on every SDL GPU backend, as in Direct3D 9).
//
// Differences, all to reproduce what DX9 drew:
// - Half pixel: Direct3D 9 puts pixel centers at integer screen coordinates, SDL GPU (Direct3D 12,
//   Vulkan, Metal) at integer + 0.5. The position is moved by half a pixel so the same pixels are
//   covered and sampled.
// - Colors: StdVertexTL's 0xAARRGGBB colors arrive as the bytes B, G, R, A (UByte4Norm); Direct3D
//   9's D3DCOLOR declaration type swizzled them to R, G, B, A, done here with .zyxw.
// - Point size: SDL GPU draws points only of size 1, and Vulkan needs the vertex shader to write
//   it; Direct3D 9's default point size was 1 too.
//
// Vertex uniform block 0 (SDL GPU: space1): the viewport rectangle x1, y1, x2, y2 in pixels, DX9's
// g_viewport.

cbuffer Std3DViewport : register(b0, space1)
{
    float4 gViewport; // x1, y1, x2, y2
};

struct Input
{
    [[vk::location(0)]] float4 position : TEXCOORD0; // sx, sy, sz, rhw
    [[vk::location(1)]] float4 diffuse : TEXCOORD1;  // bytes B, G, R, A
    [[vk::location(2)]] float4 specular : TEXCOORD2; // bytes B, G, R, A
    [[vk::location(3)]] float2 texCoord : TEXCOORD3;
};

// Direct3D 12 links a pixel shader only when its inputs are the vertex shader's outputs in the same
// registers, leaving out trailing ones only, and the DXIL is compiled from SPIR-V, which drops the
// inputs a pixel shader doesn't read. So the outputs the pixel shaders read come first and in their
// order, and the specular color, which no pixel shader reads, comes last. The point size is a Vulkan
// and Metal builtin only; the DXIL has no such output (Direct3D 12 points are always of size 1).
struct Output
{
    [[vk::location(0)]] float4 diffuse : TEXCOORD0;
    [[vk::location(1)]] float2 texCoord : TEXCOORD1;
    [[vk::location(2)]] float4 rhw : TEXCOORD2; // DX9 kept w in a float4 too (its name says rhw)
    [[vk::location(3)]] float4 specular : TEXCOORD3;
    float4 position : SV_Position;
    [[vk::builtin("PointSize")]] float pointSize : PSIZE;
};

// common.hlsli's ScreenToClip, with the half pixel above.
float4 ScreenToClip(float4 screenPos)
{
    float width  = gViewport.z - gViewport.x;
    float height = gViewport.w - gViewport.y;

    float ndc_x  = ((screenPos.x + 0.5 - gViewport.x) / width) * 2.0 - 1.0;
    float ndc_y  = (1.0 - (screenPos.y + 0.5 - gViewport.y) / height) * 2.0 - 1.0;
    float clip_w = 1.0 / screenPos.w; // rhw = 1/w, so clip_w = 1/rhw

    float4 clip;
    clip.x = ndc_x * clip_w;
    clip.y = ndc_y * clip_w;
    clip.z = screenPos.z * clip_w; // z_ndc is 0..1, so clip_z = z_ndc * w
    clip.w = clip_w;
    return clip;
}

Output main(Input input)
{
    Output output;
    output.position  = ScreenToClip(input.position);
    output.diffuse   = input.diffuse.zyxw;
    output.specular  = input.specular.zyxw;
    output.texCoord  = input.texCoord;
    output.rhw       = output.position.w;
    output.pointSize = 1.0;
    return output;
}
