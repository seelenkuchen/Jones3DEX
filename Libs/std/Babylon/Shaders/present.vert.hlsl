// Added: the present pass of stdRenderBabylon.cpp. Makes one triangle that covers the whole target
// from the vertex index alone (no vertex buffer): vertices 0, 1 and 2 land at clip (-1, 1), (3, 1)
// and (-1, -3), with texture coordinates (0, 0), (2, 0) and (0, 2), so the visible part maps the
// back buffer's top left texel to the target's top left corner.

struct Output
{
    float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(uint vertexId : SV_VertexID)
{
    Output output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv.x * 2.0 - 1.0, 1.0 - output.uv.y * 2.0, 0.0, 1.0);
    return output;
}
