// Added: the caster-mask pass of stdRenderBabylon's character-shadow stage (M11.2g), used with
// std3d.vert.hlsl and std3d_lit.vert.hlsl. The adapter re-draws the frame's caster draws against the
// scene depth target; every fragment that passes the depth test writes 1 (the mask), so a caster's
// own visible surface is marked. The receive pass then leaves those pixels out of the shadow.
//
// The shader reads no texture and declares no input. A pixel shader with no inputs is a valid prefix
// of the vertex outputs for Direct3D 12's linkage, which keeps it from rejecting the pipeline.

float4 main() : SV_Target0
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
