// Added: the vertex shader of std3D_DrawRenderListLit (std3DBabylon.cpp): std3d.vert.hlsl for
// StdVertexLit, which adds the view-space position and normal of each vertex (option A, ADR-0023).
//
// The position, colors and texture coordinates are computed exactly as std3d.vert.hlsl computes
// them (see there for the DX9 rules), so the color the lit fragment shader draws is the default
// one, bit for bit. The view-space position and normal arrive in rdroid's camera space (x right,
// y forward, z up) and leave in the normal target's (stdRenderBabylon.h: x right, y up, z toward the
// viewer), as (x, z, -y). The normal is passed as it is: the fragment shader normalizes the
// interpolated one.
//
// Vertex uniform block 0 (SDL GPU: space1): the viewport rectangle, as std3d.vert.hlsl.

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
    [[vk::location(4)]] float3 viewPosition : TEXCOORD4; // rdroid camera space
    [[vk::location(5)]] float3 viewNormal : TEXCOORD5;   // rdroid camera space
};

// The outputs std3d_lit.frag.hlsl reads come first and in its order (Direct3D 12 links a pixel shader
// only to a prefix of the vertex outputs, see std3d.vert.hlsl): those of std3d.vert.hlsl, then the
// normal. The view-space position (for the per-pixel lighting to come) and the specular color, which
// no pixel shader reads yet, trail. Lit draws are triangles only: no point size.
struct Output
{
    [[vk::location(0)]] float4 diffuse : TEXCOORD0;
    [[vk::location(1)]] float2 texCoord : TEXCOORD1;
    [[vk::location(2)]] float4 rhw : TEXCOORD2;
    [[vk::location(3)]] float3 normal : TEXCOORD3;
    [[vk::location(4)]] float3 viewPosition : TEXCOORD4;
    [[vk::location(5)]] float4 specular : TEXCOORD5;
    float4 position : SV_Position;
};

// std3d.vert.hlsl's ScreenToClip.
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

// rdroid's camera space (x right, y forward, z up) to the normal target's (x right, y up, z toward
// the viewer).
float3 ToTargetSpace(float3 v)
{
    return float3(v.x, v.z, -v.y);
}

Output main(Input input)
{
    Output output;
    output.position     = ScreenToClip(input.position);
    output.diffuse      = input.diffuse.zyxw;
    output.specular     = input.specular.zyxw;
    output.texCoord     = input.texCoord;
    output.rhw          = output.position.w;
    output.normal       = ToTargetSpace(input.viewNormal);
    output.viewPosition = ToTargetSpace(input.viewPosition);
    return output;
}
