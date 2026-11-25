// The std3DBabylon post chain and present overlays.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

namespace
{
    // The post pass pipeline stdRenderBabylon_AddPostPass makes of a fragment shader: the present
    // pass's full-screen triangle into a target of the scene's format, nothing else.
    render::PipelineDesc ExpectedPostPipeline(const render::ShaderDesc& fragmentShader)
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = fragmentShader;
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_GetColorFormat(), {} } };
        return desc;
    }

    // Checks the post pass recorded from index on: a pass on its own target, its pipeline, the inputs
    // in slots 0 up with one sampler, one triangle. Returns the target.
    render::TextureHandle AssertPostPass(size_t index, render::PipelineHandle pipeline, const std::vector<render::TextureHandle>& aInputs)
    {
        const render::BeginPass& begin = RecordedAt<render::BeginPass>(index);
        TEST_ASSERT_EQUAL_size_t(1u, begin.desc.colorTargets.size());
        TEST_ASSERT_FALSE(begin.desc.depthTarget.has_value());
        const render::ColorAttachment& attachment = begin.desc.colorTargets[0];
        TEST_ASSERT_FALSE(attachment.target.isNull());
        TEST_ASSERT_FALSE(attachment.target == stdRenderBabylon_GetColorTarget());
        TEST_ASSERT_TRUE(attachment.load == render::LoadOp::DontCare);
        TEST_ASSERT_TRUE(attachment.store == render::StoreOp::Store);
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(index + 1u) == render::SetPipeline{ pipeline });
        const render::SamplerHandle sampler = RecordedAt<render::BindTexture>(index + 2u).sampler;
        TEST_ASSERT_FALSE(sampler.isNull());
        for ( size_t slot = 0; slot < aInputs.size(); ++slot )
        {
            TEST_ASSERT_TRUE(RecordedAt<render::BindTexture>(index + 2u + slot)
                == (render::BindTexture{ render::ShaderStage::Fragment, (uint32_t)slot, aInputs[slot], sampler }));
        }
        TEST_ASSERT_TRUE(RecordedAt<render::Draw>(index + 2u + aInputs.size()) == (render::Draw{ 3u, 0u }));
        RecordedAt<render::EndPass>(index + 3u + aInputs.size());
        return attachment.target;
    }

    // A present overlay that records one pass to its target and remembers what it was given.
    struct OverlayCall
    {
        int calls = 0;
        StdRenderBabylonOverlayTarget target{};
    };

    bool RecordOverlay(void* pUser, render::CommandList& list,
        const StdRenderBabylonOverlayTarget& target)
    {
        OverlayCall* pCall = static_cast<OverlayCall*>(pUser);
        if ( !pCall )
        {
            return false;
        }
        ++pCall->calls;
        pCall->target = target;

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target.texture, 0,
            render::LoadOp::Load, render::StoreOp::Store, {} });
        return list.beginPass(pass) && list.endPass();
    }
}

TEST_GROUP(std3DBabylon);

void TEST_std3DBabylon_SETUP(void);
void TEST_std3DBabylon_TEAR_DOWN(void);

TEST(std3DBabylon, AnEmptyPostChainPresentsTheBackBuffer)
{
    OpenStd3D();
    TEST_ASSERT_EQUAL_size_t(0u, stdRenderBabylon_GetNumPostPasses());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());

    // The scene pass and the submit, nothing else.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());
}

TEST(std3DBabylon, APostPassRunsAtEachPresentAndLeavesTheBackBuffer)
{
    OpenStd3D(16u, 8u);
    const render::ShaderDesc& invert = std3DBabylonTest::shaders::postInvertFrag();
    TEST_ASSERT_EQUAL_UINT32(1u, invert.textureCount);
    TEST_ASSERT_EQUAL_UINT32(0u, invert.uniformCount);
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(invert));
    TEST_ASSERT_EQUAL_size_t(1u, stdRenderBabylon_GetNumPostPasses());
    TEST_ASSERT_EQUAL_size_t(1u, Null().recorded().size());
    const render::PipelineCreated& created = RecordedAt<render::PipelineCreated>(0);
    TEST_ASSERT_TRUE(created.desc == ExpectedPostPipeline(invert));
    const render::PipelineHandle pipeline = created.pipeline;

    // The scene, then the pass reading the back buffer color, then the submit.
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(8u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    const render::TextureHandle output = AssertPostPass(2, pipeline, { stdRenderBabylon_GetColorTarget() });
    AssertSubmitted(7);
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == output);
    const size_t postBytes = 16u * 8u
        * render::bytesPerTexel(stdRenderBabylon_GetColorFormat());
    TEST_ASSERT_EQUAL_size_t(postBytes, Null().readTexture(output, 0).size());

    // Reading the back buffer flushes without the chain and gets the back buffer.
    TEST_ASSERT_TRUE(Null().uploadTexture(output, 0, std::vector<std::byte>(postBytes, std::byte{ 0x77 })));
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
    std::vector<std::byte> aPixels(16u * 8u * 4u);
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(aPixels.data(), nullptr, nullptr));
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertSubmitted(2);
    // Through the translation to Bgra8 the scene target is the cleared black.
    AssertBytes(std::vector<std::byte>(16u * 8u * 4u, std::byte{ 0 }), aPixels);

    // A present without anything recorded runs the chain too, on the same target.
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(6u, Null().recorded().size());
    TEST_ASSERT_TRUE(AssertPostPass(0, pipeline, { stdRenderBabylon_GetColorTarget() }) == output);

    // Cleared, the chain is gone: its pipeline and targets, and the present reads the back buffer.
    stdRenderBabylon_ClearPostPasses();
    TEST_ASSERT_EQUAL_size_t(0u, stdRenderBabylon_GetNumPostPasses());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());
    TEST_ASSERT_TRUE(ReadTexture(output, 0).empty());
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(1u, Null().recorded().size());
    AssertSubmitted(0);
}

TEST(std3DBabylon, PostPassesChainAndReadTheNormalAndDepthTargets)
{
    OpenStd3D();
    const render::ShaderDesc& normalPass = std3DBabylonTest::shaders::postNormalFrag();
    TEST_ASSERT_EQUAL_UINT32(2u, normalPass.textureCount);
    // A shader that reads depth too: the Null device takes any blob, so invert's code serves.
    render::ShaderDesc depthPass = std3DBabylonTest::shaders::postInvertFrag();
    depthPass.textureCount = 3u;
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(normalPass));
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(depthPass));
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postInvertFrag()));
    TEST_ASSERT_EQUAL_size_t(3u, stdRenderBabylon_GetNumPostPasses());
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    const render::PipelineHandle aPipelines[3] = { RecordedAt<render::PipelineCreated>(0).pipeline,
        RecordedAt<render::PipelineCreated>(1).pipeline, RecordedAt<render::PipelineCreated>(2).pipeline };
    TEST_ASSERT_TRUE(RecordedAt<render::PipelineCreated>(1).desc == ExpectedPostPipeline(depthPass));

    // In order, each reading the one before; two targets in turn.
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(2u + 6u + 7u + 5u + 1u, Null().recorded().size());
    const render::TextureHandle normal = stdRenderBabylon_GetNormalTarget();
    const render::TextureHandle depth  = stdRenderBabylon_GetDepthTarget();
    const render::TextureHandle first  = AssertPostPass(2, aPipelines[0], { stdRenderBabylon_GetColorTarget(), normal });
    const render::TextureHandle second = AssertPostPass(8, aPipelines[1], { first, normal, depth });
    const render::TextureHandle third  = AssertPostPass(15, aPipelines[2], { second });
    AssertSubmitted(20);
    TEST_ASSERT_FALSE(first == second);
    TEST_ASSERT_TRUE(third == first);
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == third);

    // A new display size frees the targets; the mode set's present makes them at the new size.
    stdDisplay_SetDefaultResolution(8u, 4u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    TEST_ASSERT_TRUE(ReadTexture(first, 0).empty());
    TEST_ASSERT_TRUE(ReadTexture(second, 0).empty());
    TEST_ASSERT_FALSE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());
    TEST_ASSERT_EQUAL_size_t(8u * 4u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        Null().readTexture(stdRenderBabylon_GetPresentSource(), 0).size());
}

TEST(std3DBabylon, BadPostPassesAreRefused)
{
    // Not open.
    TEST_ASSERT_FALSE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postInvertFrag()));
    TEST_ASSERT_EQUAL_size_t(0u, stdRenderBabylon_GetNumPostPasses());
    stdRenderBabylon_ClearPostPasses();

    OpenStd3D();
    render::ShaderDesc noTexture = std3DBabylonTest::shaders::postInvertFrag();
    noTexture.textureCount = 0u;
    render::ShaderDesc fourTextures = noTexture;
    fourTextures.textureCount = 4u;
    render::ShaderDesc uniforms = std3DBabylonTest::shaders::postInvertFrag();
    uniforms.uniformCount = 1u;
    render::ShaderDesc noCode = std3DBabylonTest::shaders::postInvertFrag();
    noCode.blobs.clear();
    for ( const render::ShaderDesc* pDesc : { &noTexture, &fourTextures, &uniforms, &noCode } )
    {
        TEST_ASSERT_FALSE(stdRenderBabylon_AddPostPass(*pDesc));
    }
    TEST_ASSERT_EQUAL_size_t(0u, stdRenderBabylon_GetNumPostPasses());
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size());
}

TEST(std3DBabylon, AntiAliasingAddsThreeSmaaPassesBeforeThePresent)
{
    OpenStd3D(16u, 8u);
    const render::TextureHandle target = Null().createTexture(
        render::TextureDesc{ 16u, 8u, 1u, render::TextureFormat::Bgra8,
            render::TextureUsage::ColorTarget });
    TEST_ASSERT_FALSE(target.isNull());

    // Off (the default without a started config): only the final present pass.
    TEST_ASSERT_FALSE(stdRenderBabylon_GetAntiAliasing());
    Null().clearRecorded();
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 16u, 8u));
    std::vector<render::BeginPass> aOffPasses;
    for ( const render::RecordedCommand& command : Null().recorded() )
    {
        if ( const render::BeginPass* pPass = std::get_if<render::BeginPass>(&command) )
        {
            aOffPasses.push_back(*pPass);
        }
    }
    TEST_ASSERT_EQUAL_size_t(1u, aOffPasses.size());
    TEST_ASSERT_TRUE(aOffPasses[0].desc.colorTargets[0].target == target);

    // On: the tone map into an LDR target, edges, weights, then the blend into the target.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetAntiAliasing());
    Null().clearRecorded();
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 16u, 8u));

    std::vector<render::BeginPass> aPasses;
    for ( const render::RecordedCommand& command : Null().recorded() )
    {
        if ( const render::BeginPass* pPass = std::get_if<render::BeginPass>(&command) )
        {
            aPasses.push_back(*pPass);
        }
    }
    TEST_ASSERT_EQUAL_size_t(4u, aPasses.size());
    // The first pass is the tone map into an LDR target, not the capture target.
    TEST_ASSERT_FALSE(aPasses[0].desc.colorTargets[0].target == target);
    // The last pass writes the capture target from the anti-aliased image.
    TEST_ASSERT_EQUAL_size_t(1u, aPasses[3].desc.colorTargets.size());
    TEST_ASSERT_TRUE(aPasses[3].desc.colorTargets[0].target == target);

    Null().destroyTexture(target);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
}

TEST(std3DBabylon, SharpeningAddsACasPassAfterTheToneMapOrSmaa)
{
    OpenStd3D(16u, 8u);
    const render::TextureHandle target = Null().createTexture(
        render::TextureDesc{ 16u, 8u, 1u, render::TextureFormat::Bgra8,
            render::TextureUsage::ColorTarget });
    TEST_ASSERT_FALSE(target.isNull());

    auto passes = []
    {
        std::vector<render::BeginPass> aPasses;
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            if ( const render::BeginPass* pPass = std::get_if<render::BeginPass>(&command) )
            {
                aPasses.push_back(*pPass);
            }
        }
        return aPasses;
    };

    // Off (the default without a started config): only the final present pass.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdRenderBabylon_GetSharpening());
    Null().clearRecorded();
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 16u, 8u));
    TEST_ASSERT_EQUAL_size_t(1u, passes().size());

    // Without anti-aliasing: the tone map into the sharpening source, then CAS into the target.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.5f));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, stdRenderBabylon_GetSharpening());
    Null().clearRecorded();
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 16u, 8u));
    std::vector<render::BeginPass> aPasses = passes();
    TEST_ASSERT_EQUAL_size_t(2u, aPasses.size());
    TEST_ASSERT_FALSE(aPasses[0].desc.colorTargets[0].target == target);
    TEST_ASSERT_TRUE(aPasses[1].desc.colorTargets[0].target == target);

    // With anti-aliasing: tone map, edges, weights, the SMAA blend into the sharpening source, CAS.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(true));
    Null().clearRecorded();
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 16u, 8u));
    aPasses = passes();
    TEST_ASSERT_EQUAL_size_t(5u, aPasses.size());
    TEST_ASSERT_FALSE(aPasses[0].desc.colorTargets[0].target == target);
    TEST_ASSERT_FALSE(aPasses[3].desc.colorTargets[0].target == target);
    TEST_ASSERT_TRUE(aPasses[4].desc.colorTargets[0].target == target);

    Null().destroyTexture(target);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
}

TEST(std3DBabylon, BloomAddsAPrefilterBlurAndCompositeBeforeTheToneMap)
{
    OpenStd3D(16u, 8u);
    const render::TextureHandle target = Null().createTexture(
        render::TextureDesc{ 16u, 8u, 1u, render::TextureFormat::Bgra8,
            render::TextureUsage::ColorTarget });
    TEST_ASSERT_FALSE(target.isNull());

    auto passes = []
    {
        std::vector<render::BeginPass> aPasses;
        std::vector<std::vector<render::PushUniforms>> aUniforms;
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            if ( const render::BeginPass* pPass = std::get_if<render::BeginPass>(&command) )
            {
                aPasses.push_back(*pPass);
                aUniforms.emplace_back();
            }
            else if ( const render::PushUniforms* pUniforms = std::get_if<render::PushUniforms>(&command) )
            {
                if ( !aUniforms.empty() )
                {
                    aUniforms.back().push_back(*pUniforms);
                }
            }
        }
        return std::make_pair(aPasses, aUniforms);
    };

    // Off (the default without a started config): the scene pass only; the present source is the
    // back buffer.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdRenderBabylon_GetBloom());
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(1u, passes().first.size());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());

    // On: the scene, then half-size prefilter, four downsamples, four additive upsamples and the
    // scene-sized composite, all before the tone map.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.3f));
    TEST_ASSERT_EQUAL_FLOAT(0.3f, stdRenderBabylon_GetBloom());
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();

    const auto [aPasses, aUniforms] = passes();
    TEST_ASSERT_EQUAL_size_t(11u, aPasses.size()); // the scene and ten bloom passes
    const size_t texelBytes = render::bytesPerTexel(stdRenderBabylon_GetColorFormat());
    auto texels = [texelBytes](const render::BeginPass& pass)
    {
        return Null().readTexture(pass.desc.colorTargets[0].target, 0).size() / texelBytes;
    };

    // The prefilter target is half the scene; the threshold and soft knee reach its shader.
    TEST_ASSERT_EQUAL_size_t(8u * 4u, texels(aPasses[1]));
    TEST_ASSERT_EQUAL_size_t(1u, aUniforms[1].size());
    TEST_ASSERT_EQUAL_size_t(16u, aUniforms[1][0].data.size());
    float aPrefilter[4];
    std::memcpy(aPrefilter, aUniforms[1][0].data.data(), sizeof(aPrefilter));
    TEST_ASSERT_EQUAL_FLOAT(0.6f, aPrefilter[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, aPrefilter[1]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 16.0f, aPrefilter[2]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 8.0f, aPrefilter[3]);

    // Downsample chain: 1/4, 1/8, 1/16, 1/32 of the scene, each replacing its target.
    const size_t aDownSizes[] = { 4u * 2u, 2u * 1u, 1u * 1u, 1u * 1u };
    for ( size_t i = 0; i < sizeof(aDownSizes) / sizeof(aDownSizes[0]); ++i )
    {
        TEST_ASSERT_EQUAL_size_t(aDownSizes[i], texels(aPasses[2u + i]));
        TEST_ASSERT_TRUE(aPasses[2u + i].desc.colorTargets[0].load == render::LoadOp::DontCare);
    }

    // Upsamples run from 1/32 back to 1/2, adding into the downsampled target.
    for ( size_t i = 0; i < 4u; ++i )
    {
        TEST_ASSERT_TRUE(aPasses[6u + i].desc.colorTargets[0].load == render::LoadOp::Load);
    }
    TEST_ASSERT_EQUAL_size_t(8u * 4u, texels(aPasses[9]));

    // The composite is scene sized and feeds the tone map.
    TEST_ASSERT_EQUAL_size_t(16u * 8u, texels(aPasses[10]));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == aPasses[10].desc.colorTargets[0].target);
    TEST_ASSERT_EQUAL_size_t(1u, aUniforms[10].size());
    float aComposite[4];
    std::memcpy(aComposite, aUniforms[10][0].data.data(), sizeof(aComposite));
    TEST_ASSERT_EQUAL_FLOAT(0.3f, aComposite[0]);

    // A capture reads the composite and adds only the final tone-map pass into its target.
    Null().clearRecorded();
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 16u, 8u));
    const auto [aCapturePasses, aCaptureUniforms] = passes();
    TEST_ASSERT_EQUAL_size_t(1u, aCapturePasses.size());
    TEST_ASSERT_TRUE(aCapturePasses[0].desc.colorTargets[0].target == target);

    // Off again skips the stage and presents the back buffer.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(1u, passes().first.size());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());

    Null().destroyTexture(target);
}

TEST(std3DBabylon, ThePresentOverlayRecordsAfterTheScenePass)
{
    OpenStd3D(16u, 8u);
    OverlayCall call;
    stdRenderBabylon_SetPresentOverlay(RecordOverlay, &call);
    TEST_ASSERT_TRUE(stdRenderBabylon_HasPresentOverlay());

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();

    TEST_ASSERT_EQUAL_INT(1, call.calls);
    TEST_ASSERT_TRUE(call.target.texture.isNull());
    TEST_ASSERT_EQUAL_UINT32(16u, call.target.width);
    TEST_ASSERT_EQUAL_UINT32(8u, call.target.height);

    TEST_ASSERT_EQUAL_size_t(5u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    const render::BeginPass& overlay = RecordedAt<render::BeginPass>(2);
    TEST_ASSERT_EQUAL_size_t(1u, overlay.desc.colorTargets.size());
    TEST_ASSERT_TRUE(overlay.desc.colorTargets[0].target.isNull());
    TEST_ASSERT_TRUE(overlay.desc.colorTargets[0].load == render::LoadOp::Load);
    RecordedAt<render::EndPass>(3);
    AssertSubmitted(4);

    // A flush frame does not run the overlay.
    call.calls = 0;
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
    TEST_ASSERT_TRUE(stdRenderBabylon_FlushFrame());
    TEST_ASSERT_EQUAL_INT(0, call.calls);
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Load, render::LoadOp::Load);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);

    stdRenderBabylon_SetPresentOverlay(nullptr, nullptr);
    TEST_ASSERT_FALSE(stdRenderBabylon_HasPresentOverlay());
}

TEST(std3DBabylon, TheHudOverlayRecordsBeforeThePresentOverlay)
{
    OpenStd3D(16u, 8u);
    OverlayCall hud;
    OverlayCall menu;
    stdRenderBabylon_SetHudOverlay(RecordOverlay, &hud);
    stdRenderBabylon_SetPresentOverlay(RecordOverlay, &menu);
    TEST_ASSERT_TRUE(stdRenderBabylon_HasHudOverlay());
    TEST_ASSERT_TRUE(stdRenderBabylon_HasPresentOverlay());

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();

    TEST_ASSERT_EQUAL_INT(1, hud.calls);
    TEST_ASSERT_EQUAL_INT(1, menu.calls);
    TEST_ASSERT_EQUAL_size_t(7u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    const render::BeginPass& hudPass = RecordedAt<render::BeginPass>(2);
    TEST_ASSERT_TRUE(hudPass.desc.colorTargets[0].target.isNull());
    TEST_ASSERT_TRUE(hudPass.desc.colorTargets[0].load == render::LoadOp::Load);
    RecordedAt<render::EndPass>(3);
    RecordedAt<render::BeginPass>(4);
    RecordedAt<render::EndPass>(5);
    AssertSubmitted(6);

    // A flush frame runs neither overlay.
    hud.calls = 0;
    menu.calls = 0;
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
    TEST_ASSERT_TRUE(stdRenderBabylon_FlushFrame());
    TEST_ASSERT_EQUAL_INT(0, hud.calls);
    TEST_ASSERT_EQUAL_INT(0, menu.calls);

    stdRenderBabylon_SetPresentOverlay(nullptr, nullptr);
    stdRenderBabylon_SetHudOverlay(nullptr, nullptr);
    TEST_ASSERT_FALSE(stdRenderBabylon_HasHudOverlay());
}

TEST(std3DBabylon, APresentToTextureIncludesTheOverlay)
{
    OpenStd3D(16u, 8u);
    const render::TextureHandle target = Null().createTexture(
        render::TextureDesc{ 32u, 16u, 1u, render::TextureFormat::Bgra8,
            render::TextureUsage::ColorTarget });
    TEST_ASSERT_FALSE(target.isNull());

    // The Null device has no swapchain, so the first capture makes the
    // present pipeline; the second one starts with a clean record.
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 32u, 16u));
    Null().clearRecorded();

    OverlayCall call;
    stdRenderBabylon_SetPresentOverlay(RecordOverlay, &call);
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 32u, 16u));

    TEST_ASSERT_EQUAL_INT(1, call.calls);
    TEST_ASSERT_TRUE(call.target.texture == target);
    TEST_ASSERT_EQUAL_UINT32(32u, call.target.width);
    TEST_ASSERT_EQUAL_UINT32(16u, call.target.height);

    // The final tone-map pass into the capture target (pipeline, input, lookup, parameters,
    // triangle), then the overlay's pass on it, then one submit.
    const render::BeginPass& present = RecordedAt<render::BeginPass>(0);
    TEST_ASSERT_TRUE(present.desc.colorTargets[0].target == target);
    TEST_ASSERT_TRUE(present.desc.colorTargets[0].load == render::LoadOp::DontCare);
    RecordedAt<render::EndPass>(6);
    const render::BeginPass& overlay = RecordedAt<render::BeginPass>(7);
    TEST_ASSERT_TRUE(overlay.desc.colorTargets[0].target == target);
    TEST_ASSERT_TRUE(overlay.desc.colorTargets[0].load == render::LoadOp::Load);
    RecordedAt<render::EndPass>(8);
    AssertSubmitted(9);
    TEST_ASSERT_EQUAL_size_t(32u * 16u * 4u, Null().readTexture(target, 0).size());

    stdRenderBabylon_SetPresentOverlay(nullptr, nullptr);
    Null().destroyTexture(target);
}

TEST(std3DBabylon, APresentToTextureIncludesTheHudOverlay)
{
    OpenStd3D(16u, 8u);
    const render::TextureHandle target = Null().createTexture(
        render::TextureDesc{ 32u, 16u, 1u, render::TextureFormat::Bgra8,
            render::TextureUsage::ColorTarget });
    TEST_ASSERT_FALSE(target.isNull());

    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 32u, 16u));
    Null().clearRecorded();

    OverlayCall hud;
    stdRenderBabylon_SetHudOverlay(RecordOverlay, &hud);
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, 32u, 16u));

    TEST_ASSERT_EQUAL_INT(1, hud.calls);
    TEST_ASSERT_TRUE(hud.target.texture == target);
    // The final tone-map pass, then the HUD overlay's pass on the capture target, then one submit.
    RecordedAt<render::EndPass>(6);
    const render::BeginPass& overlay = RecordedAt<render::BeginPass>(7);
    TEST_ASSERT_TRUE(overlay.desc.colorTargets[0].target == target);
    TEST_ASSERT_TRUE(overlay.desc.colorTargets[0].load == render::LoadOp::Load);
    RecordedAt<render::EndPass>(8);
    AssertSubmitted(9);

    stdRenderBabylon_SetHudOverlay(nullptr, nullptr);
    Null().destroyTexture(target);
}

TEST(std3DBabylon, ToneMapParametersValidateAndDefault)
{
    OpenStd3D();
    const StdRenderBabylonToneMap defaults = stdRenderBabylon_GetToneMap();
    TEST_ASSERT_EQUAL_FLOAT(1.0f, defaults.exposure);
    TEST_ASSERT_EQUAL_UINT32(0u, defaults.oper);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, defaults.brightness);
    TEST_ASSERT_EQUAL_UINT32(0u, defaults.colorGrading);

    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 0.5f, 0u });
    TEST_ASSERT_EQUAL_FLOAT(0.5f, stdRenderBabylon_GetToneMap().exposure);
    TEST_ASSERT_EQUAL_UINT32(0u, stdRenderBabylon_GetToneMap().oper);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdRenderBabylon_GetToneMap().brightness);

    // Operators 1 (Neutral) and 2 (ACES fitted) are accepted; anything else changes nothing.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 2.0f, 1u });
    TEST_ASSERT_EQUAL_FLOAT(2.0f, stdRenderBabylon_GetToneMap().exposure);
    TEST_ASSERT_EQUAL_UINT32(1u, stdRenderBabylon_GetToneMap().oper);
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.5f, 2u });
    TEST_ASSERT_EQUAL_FLOAT(1.5f, stdRenderBabylon_GetToneMap().exposure);
    TEST_ASSERT_EQUAL_UINT32(2u, stdRenderBabylon_GetToneMap().oper);
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 0.25f, 3u });
    TEST_ASSERT_EQUAL_FLOAT(1.5f, stdRenderBabylon_GetToneMap().exposure);
    TEST_ASSERT_EQUAL_UINT32(2u, stdRenderBabylon_GetToneMap().oper);

    // Brightness is a positive finite gamma; a bad value changes nothing.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.5f });
    TEST_ASSERT_EQUAL_FLOAT(1.5f, stdRenderBabylon_GetToneMap().brightness);
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 0.0f });
    TEST_ASSERT_EQUAL_FLOAT(1.5f, stdRenderBabylon_GetToneMap().brightness);

    // Color grading is preset 0..5; anything above 5 changes nothing.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.5f, 3u });
    TEST_ASSERT_EQUAL_UINT32(3u, stdRenderBabylon_GetToneMap().colorGrading);
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.5f, 6u });
    TEST_ASSERT_EQUAL_UINT32(3u, stdRenderBabylon_GetToneMap().colorGrading);

    // The defaults return when the frame owner is closed and opened again.
    CloseStd3D();
    OpenStd3D();
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdRenderBabylon_GetToneMap().exposure);
    TEST_ASSERT_EQUAL_UINT32(0u, stdRenderBabylon_GetToneMap().oper);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdRenderBabylon_GetToneMap().brightness);
    TEST_ASSERT_EQUAL_UINT32(0u, stdRenderBabylon_GetToneMap().colorGrading);
}

TEST(std3DBabylon, CloseClearsThePresentOverlay)
{
    OpenStd3D(8u, 8u);
    OverlayCall call;
    stdRenderBabylon_SetPresentOverlay(RecordOverlay, &call);
    TEST_ASSERT_TRUE(stdRenderBabylon_HasPresentOverlay());

    CloseStd3D();
    TEST_ASSERT_FALSE(stdRenderBabylon_HasPresentOverlay());

    OpenStd3D(8u, 8u);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_INT(0, call.calls);
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
}

TEST(std3DBabylon, FocusSmoothingReachesTheTarget)
{
    OpenStd3D();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(true, true, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 1.0f, 100.0f));

    // The first input sets the distance; later ones approach the target over about 0.2 s.
    std3D_SetFocus(20.0f, false, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, stdRenderBabylon_GetFocusDistance());
    std3D_SetFocus(60.0f, false, 0.1f);
    TEST_ASSERT_TRUE(stdRenderBabylon_GetFocusDistance() > 20.0f);
    TEST_ASSERT_TRUE(stdRenderBabylon_GetFocusDistance() < 60.0f);
    std3D_SetFocus(60.0f, false, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 60.0f, stdRenderBabylon_GetFocusDistance());
    for ( int i = 0; i < 16; ++i )
    {
        std3D_SetFocus(60.0f, false, 0.1f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, stdRenderBabylon_GetFocusDistance());

    // The cutscene flag changes the ramp only, not the smoothed distance.
    std3D_SetFocus(10.0f, true, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, stdRenderBabylon_GetFocusDistance());
}
