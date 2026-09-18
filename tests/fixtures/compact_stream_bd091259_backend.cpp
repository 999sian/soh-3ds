// Immutable dirty-bd091259 oracle excerpts; retain original bodies verbatim.
// Source: platform/3ds/source/gfx_citro3d.cpp
// Full source SHA-256: 5985b782d08d48cb9a175e3e8d5a43437d470a64b9fe51cbed37f43231ab5b58

uint8_t FloatColorToByte(float value) {
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

void PackGenericVertices(const float* drawVertices, PackedVertex* packedVertices, size_t vertexCount,
                         const ShaderProgram* program, int varyingInput, float coverScale,
                         const std::array<float, 2>& textureScaleU,
                         const std::array<float, 2>& textureScaleV,
                         const std::array<float, 2>& textureOffsetV,
                         const std::array<bool, 2>& rotatedFramebuffer) {
    for (size_t vertex = 0; vertex < vertexCount; ++vertex) {
        const float* source = drawVertices + vertex * program->strideFloats;
        PackedVertex& destination = packedVertices[vertex];
        destination.position[0] = source[0] * coverScale;
        destination.position[1] = source[1] * coverScale;
        destination.position[2] = source[2];
        destination.position[3] = source[3];

        for (int texture = 0; texture < 2; ++texture) {
            float u = 0.0f;
            float v = 0.0f;
            if (program->usedTextures[texture]) {
                const uint8_t textureOffset = program->textureOffsets[texture];
                u = source[textureOffset];
                v = source[textureOffset + 1];
                uint8_t clampOffset = textureOffset + 2;
                if (program->clamp[texture][0]) {
                    u = std::min(u, source[clampOffset++]);
                }
                if (program->clamp[texture][1]) {
                    v = std::min(v, source[clampOffset]);
                }
                if (rotatedFramebuffer[texture]) {
                    const float uprightU = std::clamp(u, 0.0f, 1.0f);
                    const float uprightV = std::clamp(v, 0.0f, 1.0f);
                    u = (1.0f - uprightV) * textureScaleU[texture];
                    v = 1.0f - textureOffsetV[texture] - uprightU * textureScaleV[texture];
                } else {
                    u *= textureScaleU[texture];
                    v *= textureScaleV[texture];
                }
            }
            float* texcoord = texture == 0 ? destination.texcoord0 : destination.texcoord1;
            texcoord[0] = u;
            texcoord[1] = v;
        }

        if (varyingInput >= 0) {
            const float* color = source + program->inputOffsets[varyingInput];
            destination.color[0] = FloatColorToByte(color[0]);
            destination.color[1] = FloatColorToByte(color[1]);
            destination.color[2] = FloatColorToByte(color[2]);
            destination.color[3] = program->alpha ? FloatColorToByte(color[3]) : 255;
        } else {
            destination.color[0] = 255;
            destination.color[1] = 255;
            destination.color[2] = 255;
            destination.color[3] = 255;
        }
        // Fog rides the vertex alpha: PICA has one interpolated colour, and
        // Fast3D forces the shade alpha to 1.0 whenever it emits fog, so the
        // channel is free (the alpha TEV side reads that 1.0 as a constant).
        if (program->fog) {
            destination.color[3] = FloatColorToByte(source[program->fogOffset + 3]);
        }
    }
}

size_t ClipTriangleAgainstW(const float* vertices[3], size_t stride, float* output) {
    // Every emitted component below is assigned before it is read. Avoid
    // clearing the full 1 KiB maximum scratch polygon for each clipped
    // triangle on the CPU hot path.
    std::array<std::array<float, kMaxVertexStrideFloats>, 4> polygon;
    int polygonCount = 0;

    for (int index = 0; index < 3; ++index) {
        const float* current = vertices[index];
        const float* next = vertices[(index + 1) % 3];
        const float currentDistance = current[2] + current[3];
        const float nextDistance = next[2] + next[3];
        const bool currentInside = currentDistance >= 0.0f;
        const bool nextInside = nextDistance >= 0.0f;
        if (currentInside) {
            std::copy_n(current, stride, polygon[polygonCount++].begin());
        }
        if (currentInside != nextInside) {
            const float amount = currentDistance / (currentDistance - nextDistance);
            auto& clipped = polygon[polygonCount++];
            for (size_t component = 0; component < stride; ++component) {
                clipped[component] = current[component] + (next[component] - current[component]) * amount;
            }
        }
    }

    size_t outputCount = 0;
    for (int index = 2; index < polygonCount; ++index) {
        std::copy_n(polygon[0].begin(), stride, output + outputCount++ * stride);
        std::copy_n(polygon[index - 1].begin(), stride, output + outputCount++ * stride);
        std::copy_n(polygon[index].begin(), stride, output + outputCount++ * stride);
    }
    return outputCount;
}

void GfxRenderingAPICitro3D::DrawTriangles(float bufVbo[], size_t bufVboLen, size_t bufVboNumTris) {
    Soh3dsProfileScope drawProfile(Soh3dsProfileSection::Draw);
    ShaderProgram* program = mImpl->currentProgram;
    if (!mImpl->frameActive || program == nullptr || program->invisible || bufVbo == nullptr ||
        program->strideFloats == 0 || bufVboNumTris == 0 || mImpl->packedVertices == nullptr) {
        return;
    }

    const size_t sourceVertexCount = std::min(bufVboNumTris * 3, static_cast<size_t>(kMaxSourceVertices));
    const size_t sourceTriangleCount = sourceVertexCount / 3;
    if (program->strideFloats > kMaxVertexStrideFloats ||
        bufVboLen < sourceVertexCount * program->strideFloats) {
        return;
    }
    // A texture whose allocation failed (or a framebuffer never drawn to or
    // copied into) has no storage; the unit still holds whatever was bound
    // before. For unit 0 skip the draw rather than paint it with an unrelated
    // image - the interpreter re-imports on the next lookup. Unit 1 is
    // different: OoT's stock two-cycle idiom `TEXEL1 * PRIM_LOD_FRAC + COMBINED`
    // (webs, vines, scene water, most static geometry) references TEXEL1 with
    // only tile 0 loaded, so Fast3D imports a STALE tile 1 that may or may not
    // resolve from frame to frame. Desktop samples that garbage times zero;
    // skipping made those surfaces pop in and out on hardware. Bind unit 0's
    // texture there instead - deterministic, and multiplied by zero anyway.
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture]) {
            continue;
        }
        const int framebufferId = mImpl->selectedFramebuffers[texture];
        bool hasStorage;
        if (framebufferId != 0) {
            hasStorage = framebufferId > 0 && framebufferId < static_cast<int>(mImpl->framebuffers.size()) &&
                         mImpl->framebuffers[framebufferId] != nullptr && mImpl->framebuffers[framebufferId]->initialized;
        } else {
            const uint32_t textureId = mImpl->selectedTextures[texture];
            hasStorage = textureId < mImpl->textures.size() && mImpl->textures[textureId].initialized;
        }
        if (hasStorage) {
            continue;
        }
        if (texture == 0) {
            return;
        }
        const uint32_t fallbackId = mImpl->selectedTextures[0];
        if (mImpl->selectedFramebuffers[0] == 0 && fallbackId < mImpl->textures.size() &&
            mImpl->textures[fallbackId].initialized) {
            mImpl->BindTexture(1, &mImpl->textures[fallbackId].texture);
        }
    }

    const float* drawVertices = bufVbo;
    size_t vertexCount = sourceVertexCount;
    bool needsClipping = false;
    for (size_t vertex = 0; vertex < sourceVertexCount; ++vertex) {
        const float* v = bufVbo + vertex * program->strideFloats;
        if (v[2] + v[3] < 0.0f) { // in front of the near plane
            needsClipping = true;
            break;
        }
    }
    if (needsClipping) {
        mImpl->clipScratch.resize(sourceTriangleCount * 6 * program->strideFloats);
        size_t clippedVertexCount = 0;
        for (size_t triangle = 0; triangle < sourceTriangleCount; ++triangle) {
            const float* triangleVertices[3] = {
                bufVbo + (triangle * 3 + 0) * program->strideFloats,
                bufVbo + (triangle * 3 + 1) * program->strideFloats,
                bufVbo + (triangle * 3 + 2) * program->strideFloats,
            };
            const int insideCount =
                static_cast<int>(triangleVertices[0][2] + triangleVertices[0][3] >= 0.0f) +
                static_cast<int>(triangleVertices[1][2] + triangleVertices[1][3] >= 0.0f) +
                static_cast<int>(triangleVertices[2][2] + triangleVertices[2][3] >= 0.0f);
            if (insideCount == 3) {
                std::copy_n(triangleVertices[0], 3 * program->strideFloats,
                            mImpl->clipScratch.data() +
                                clippedVertexCount * program->strideFloats);
                clippedVertexCount += 3;
                continue;
            }
            if (insideCount == 0) {
                continue;
            }
            clippedVertexCount += ClipTriangleAgainstW(
                triangleVertices, program->strideFloats,
                mImpl->clipScratch.data() + clippedVertexCount * program->strideFloats);
        }
        if (clippedVertexCount == 0) {
            return;
        }
        drawVertices = mImpl->clipScratch.data();
        vertexCount = std::min(clippedVertexCount, static_cast<size_t>(kMaxDrawVertices));
    }
    if (mImpl->packedVertexCount + vertexCount > kVertexBufferCapacity) {
        throw std::length_error("3DS packed vertex buffer exhausted");
    }

    const size_t firstVertex = mImpl->packedVertexCount;

    std::array<std::array<float, 4>, 7> constants = {};
    // A program with one combiner input can always source it from the primary
    // vertex color. This is exact for both uniform and varying batches, avoids
    // scanning every vertex to rediscover uniformity, and keeps per-draw tint
    // changes out of the six-stage TEV rebuild path.
    int varyingInput = program->numInputs == 1 ? 0 : -1;
    for (uint8_t input = 0; input < program->numInputs; ++input) {
        const uint8_t inputOffset = program->inputOffsets[input];
        for (int component = 0; component < 4; ++component) {
            // Inputs are packed rgb (3 floats) or rgba (4) by Fast3D. The old
            // `std::min(component, 2)` read the BLUE channel as alpha whenever
            // alpha was present: every constant alpha (PRIM/ENV alpha fades,
            // PRIM_LOD_FRAC) was wrong - the Deku Tree webs drew opaque white
            // because `TEXEL1 * PRIM_LOD_FRAC(=blue=1) + COMBINED` saturated.
            constants[input][component] =
                component == 3 && !program->alpha ? 1.0f : drawVertices[inputOffset + component];
        }
        // PICA exposes one primary vertex color to the TEV pipeline. Once that
        // varying input is chosen, every later input is necessarily represented
        // by its first-vertex constant, so scanning the rest of the batch cannot
        // affect the generated TEV state.
        if (varyingInput >= 0) {
            continue;
        }
        bool constant = true;
        for (size_t vertex = 1; vertex < vertexCount && constant; ++vertex) {
            const float* source = drawVertices + vertex * program->strideFloats + inputOffset;
            const int componentCount = program->alpha ? 4 : 3;
            for (int component = 0; component < componentCount; ++component) {
                if (std::fabs(source[component] - constants[input][component]) > 1.0e-5f) {
                    constant = false;
                    break;
                }
            }
        }
        if (!constant && varyingInput < 0) {
            varyingInput = input;
        }
    }
    // SoH-3DS: every input uniform (2D HUD, menus) still puts input 0 on the
    // primary colour, so a stage mixing two inputs - (PRIM - ENV) * TEXEL0 +
    // ENV is the HUD text/outline shape - needs only one GPU_CONSTANT.
    // ponytail: input 0 is a fixed pick; three uniform inputs in one stage
    // still collide (none found in OoT).
    if (varyingInput < 0 && program->numInputs > 0) {
        varyingInput = 0;
    }

    // Fast3D already applies the same aspect correction to backgrounds and
    // 3D geometry. Enlarging only a 320x240 backdrop displaces its doorways
    // relative to the door actors (for example the Market Guard House).
    const float coverScale = 1.0f;
    std::array<float, 2> textureScaleU = { 1.0f, 1.0f };
    std::array<float, 2> textureScaleV = { 1.0f, 1.0f };
    std::array<float, 2> textureOffsetV = { 0.0f, 0.0f };
    std::array<bool, 2> rotatedFramebuffer = { false, false };
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture]) {
            continue;
        }
        // SoH-3DS: SelectTextureFb parks selectedTextures at 0 (the null slot).
        // A bound framebuffer owns the unit's scale outright; scaling by both
        // made the fullscreen composite sample a corner ("zoomed in" game).
        const int framebufferId = mImpl->selectedFramebuffers[texture];
        if (framebufferId > 0 && framebufferId < static_cast<int>(mImpl->framebuffers.size()) &&
            mImpl->framebuffers[framebufferId] != nullptr &&
            mImpl->framebuffers[framebufferId]->initialized) {
            const auto& slot = *mImpl->framebuffers[framebufferId];
            if (slot.rotated) {
                // PICA samples v=0 from the final memory row. Rendered content
                // has leading rows from the POT target's rasterizer Y flip;
                // GX copies from the top LCD start at row zero instead.
                rotatedFramebuffer[texture] = true;
                textureScaleU[texture] = static_cast<float>(slot.contentHeight) / slot.texture.width;
                textureScaleV[texture] = static_cast<float>(slot.contentWidth) / slot.texture.height;
                textureOffsetV[texture] = static_cast<float>(slot.contentOffsetY) / slot.texture.height;
            } else {
                textureScaleU[texture] *= static_cast<float>(slot.contentWidth) / slot.texture.width;
                textureScaleV[texture] *= static_cast<float>(slot.contentHeight) / slot.texture.height;
            }
            continue;
        }
        const uint32_t textureId = mImpl->selectedTextures[texture];
        if (textureId < mImpl->textures.size() && mImpl->textures[textureId].initialized) {
            const auto& slot = mImpl->textures[textureId];
            textureScaleU[texture] *= static_cast<float>(slot.sourceWidth) / slot.texture.width;
            textureScaleV[texture] *= static_cast<float>(slot.sourceHeight) / slot.texture.height;
        }
    }
    {
        Soh3dsProfileScope packProfile(Soh3dsProfileSection::Pack);
        const bool commonPacked = PackVertices(drawVertices, mImpl->packedVertices + firstVertex, vertexCount, program,
                                               varyingInput, coverScale, textureScaleU, textureScaleV, textureOffsetV,
                                               rotatedFramebuffer);
        Soh3dsProfilePackBatch(commonPacked, static_cast<uint32_t>(vertexCount));
    }
    Soh3dsProfileScope stateProfile(Soh3dsProfileSection::State);
    std::array<float, 4> fogColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (program->fog) {
        std::copy_n(drawVertices + program->fogOffset, 3, fogColor.begin());
    }
    const int alphaVaryingInput = program->fog ? -1 : varyingInput;

    std::array<float, 4> grayscaleColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    if (program->grayscale) {
        const float* color = drawVertices + program->grayscaleOffset;
        std::copy_n(color, grayscaleColor.size(), grayscaleColor.begin());
    }

    std::array<uint32_t, 7> packedTevConstants = {};
    for (size_t index = 0; index < program->numInputs; ++index) {
        if (static_cast<int>(index) == varyingInput) {
            continue;
        }
        packedTevConstants[index] = PackColor(constants[index]);
    }
    const uint32_t packedTevGrayscale =
        program->grayscale ? PackColor(grayscaleColor) : 0;
    // The alpha side reads the varying input as a constant while fog owns the
    // vertex alpha, so that constant is part of the TEV state too.
    if (program->fog && varyingInput >= 0) {
        fogColor[3] = constants[varyingInput][3];
    }
    const uint32_t packedTevFog = program->fog ? PackColor(fogColor) : 0;
    const bool updateTevState = !mImpl->tevStateValid ||
                                mImpl->tevStateProgram != program ||
                                mImpl->tevStateVaryingInput != varyingInput ||
                                mImpl->tevStateConstants != packedTevConstants ||
                                mImpl->tevStateGrayscale != packedTevGrayscale ||
                                mImpl->tevStateFog != packedTevFog ||
                                mImpl->tevStateDepthTest != mImpl->depthTest ||
                                mImpl->tevStateDepthWrite != mImpl->depthWrite;
    if (updateTevState) {

    // The previous-buffer update mask is persistent Citro3D state. Reset it
    // for every draw so a grayscale program cannot leak into the next one.
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnvBufColor(0xFFFFFFFF);

    int stage = 0;
    const int cycleCount = program->twoCycle ? 2 : 1;
    for (int cycle = 0; cycle < cycleCount && stage < 6; ++cycle) {
        const ChannelPlan rgbPlan = ResolveChannelPlan(program->compiledChannels[cycle][0],
            program->combiner[cycle][0], varyingInput, constants, false);
        const ChannelPlan alphaPlan =
            program->alpha ? ResolveChannelPlan(program->compiledChannels[cycle][1],
                                               program->combiner[cycle][1], alphaVaryingInput, constants, true)
                           : SingleOperationPlan(PassPrevious());
        const size_t operationCount = std::max(rgbPlan.size(), alphaPlan.size());
        for (size_t operationIndex = 0; operationIndex < operationCount && stage < 6; ++operationIndex, ++stage) {
            const ChannelOperation rgbOperation =
                operationIndex < rgbPlan.size() ? rgbPlan[operationIndex] : PassPrevious();
            const ChannelOperation alphaOperation =
                operationIndex < alphaPlan.size() ? alphaPlan[operationIndex] : PassPrevious();
            C3D_TexEnv* environment = C3D_GetTexEnv(stage);
            C3D_TexEnvInit(environment);
            ConfigureChannel(environment, C3D_RGB, rgbOperation, varyingInput, cycle);
            ConfigureChannel(environment, C3D_Alpha, alphaOperation, alphaVaryingInput, cycle);
            ConfigureOperands(environment, rgbOperation, alphaOperation);

            std::array<float, 4> environmentColor = StageConstant(rgbPlan, rgbOperation, varyingInput, constants);
            environmentColor[3] = StageConstant(alphaPlan, alphaOperation, alphaVaryingInput, constants)[3];
            C3D_TexEnvColor(environment, PackColor(environmentColor));
        }
    }

    // Fog: Fast3D's `rgb = mix(rgb, fog.rgb, factor)` after the combiner, RGB
    // only. GPU_INTERPOLATE(s1, s2, s3) = s1*s3 + s2*(1-s3) with s1 = fog
    // colour (constant), s2 = combiner output, s3 = the per-vertex factor
    // carried in the primary colour's alpha. One stage; the alpha side passes
    // the combiner alpha through untouched, so the alpha test still sees it.
    if (program->fog && stage > 0 && stage < 6) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage++);
        C3D_TexEnvInit(environment);
        C3D_TexEnvSrc(environment, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
        C3D_TexEnvOpRgb(environment, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(environment, C3D_RGB, GPU_INTERPOLATE);
        ConfigureAlphaPass(environment);
        C3D_TexEnvColor(environment, PackColor({ fogColor[0], fogColor[1], fogColor[2], 0.0f }));
    }

    // Fast3D's grayscale option is: mix(original, tint * average(rgb),
    // tint.a). PICA has no programmable fragment shader, but its previous
    // buffer and per-source R/G/B replication make the average exact (apart
    // from the normal 8-bit TEV quantization): accumulate r/3, g/3, and b/3
    // while applying the tint. The buffer update becomes readable one stage
    // later, so the R stage reads GPU_PREVIOUS directly; the G/B stages and
    // optional final mix read the preserved base RGB from the buffer.
    const float grayscaleMix = std::clamp(grayscaleColor[3], 0.0f, 1.0f);
    const bool needsGrayscaleMix = grayscaleMix < 1.0f - (0.5f / 255.0f);
    const int grayscaleStages = needsGrayscaleMix ? 4 : 3;
    bool grayscaleApplied = false;
    if (program->grayscale && grayscaleMix > 0.5f / 255.0f && stage > 0 &&
        stage + grayscaleStages <= 6 && stage - 1 < 4) {
        grayscaleApplied = true;
        C3D_TexEnvBufUpdate(C3D_RGB, 1 << (stage - 1));

        const std::array<float, 4> scaledTint = {
            std::clamp(grayscaleColor[0], 0.0f, 1.0f) / 3.0f,
            std::clamp(grayscaleColor[1], 0.0f, 1.0f) / 3.0f,
            std::clamp(grayscaleColor[2], 0.0f, 1.0f) / 3.0f,
            grayscaleMix,
        };
        constexpr std::array<GPU_TEVOP_RGB, 3> channelOperands = {
            GPU_TEVOP_RGB_SRC_R,
            GPU_TEVOP_RGB_SRC_G,
            GPU_TEVOP_RGB_SRC_B,
        };
        for (int channel = 0; channel < 3; ++channel, ++stage) {
            C3D_TexEnv* environment = C3D_GetTexEnv(stage);
            C3D_TexEnvInit(environment);
            C3D_TexEnvSrc(environment, C3D_RGB,
                          channel == 0 ? GPU_PREVIOUS : GPU_PREVIOUS_BUFFER, GPU_CONSTANT,
                          GPU_PREVIOUS);
            C3D_TexEnvOpRgb(environment, channelOperands[channel], GPU_TEVOP_RGB_SRC_COLOR,
                            GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvFunc(environment, C3D_RGB,
                           channel == 0 ? GPU_MODULATE : GPU_MULTIPLY_ADD);
            ConfigureAlphaPass(environment);
            C3D_TexEnvColor(environment, PackColor(scaledTint));
        }

        if (needsGrayscaleMix) {
            C3D_TexEnv* environment = C3D_GetTexEnv(stage++);
            C3D_TexEnvInit(environment);
            C3D_TexEnvSrc(environment, C3D_RGB, GPU_PREVIOUS, GPU_PREVIOUS_BUFFER,
                          GPU_CONSTANT);
            C3D_TexEnvOpRgb(environment, GPU_TEVOP_RGB_SRC_COLOR,
                            GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
            C3D_TexEnvFunc(environment, C3D_RGB, GPU_INTERPOLATE);
            ConfigureAlphaPass(environment);
            C3D_TexEnvColor(environment, PackColor({ 0.0f, 0.0f, 0.0f, grayscaleMix }));
        }
    }

    // Some stock menu-background combiners consume four or five TEV stages,
    // leaving too little room for the exact three-stage luminance pass above.
    // Those draws use a fully opaque grayscale color and previously lost the
    // red/green/blue menu filter completely. Preserve the visible stock tint
    // with a one-stage modulation fallback; simpler shaders still take the
    // exact luminance path.
    if (program->grayscale && !grayscaleApplied &&
        grayscaleMix >= 1.0f - (0.5f / 255.0f) && stage > 0 && stage < 6) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage++);
        C3D_TexEnvInit(environment);
        C3D_TexEnvSrc(environment, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PREVIOUS);
        C3D_TexEnvOpRgb(environment, GPU_TEVOP_RGB_SRC_COLOR,
                       GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(environment, C3D_RGB, GPU_MODULATE);
        ConfigureAlphaPass(environment);
        C3D_TexEnvColor(environment,
                       PackColor({ std::clamp(grayscaleColor[0], 0.0f, 1.0f),
                                   std::clamp(grayscaleColor[1], 0.0f, 1.0f),
                                   std::clamp(grayscaleColor[2], 0.0f, 1.0f), 1.0f }));
    }
    for (; stage < 6; ++stage) {
        C3D_TexEnv* environment = C3D_GetTexEnv(stage);
        C3D_TexEnvInit(environment);
    }

    // Alpha-test gating and references live in ApplyAlphaTest. History: an
    // ungated GPU_GREATER 0x08 test read undefined stage-0 alpha on alpha-less
    // programs and discarded OoT's prerendered room backgrounds (z_room.c sets
    // G_AC_THRESHOLD with G_RM_NOOP) - the green Link's House / yellow shop.
    ApplyAlphaTest(program);
    C3D_DepthTest(mImpl->depthTest, mImpl->depthTest ? GPU_GREATER : GPU_ALWAYS,
                  static_cast<GPU_WRITEMASK>(GPU_WRITE_COLOR | (mImpl->depthWrite ? GPU_WRITE_DEPTH : 0)));
        mImpl->tevStateValid = true;
        mImpl->tevStateProgram = program;
        mImpl->tevStateVaryingInput = varyingInput;
        mImpl->tevStateConstants = packedTevConstants;
        mImpl->tevStateGrayscale = packedTevGrayscale;
        mImpl->tevStateFog = packedTevFog;
        mImpl->tevStateDepthTest = mImpl->depthTest;
        mImpl->tevStateDepthWrite = mImpl->depthWrite;
    }
    if (mImpl->dirtyVertexBegin == mImpl->dirtyVertexEnd) {
        mImpl->dirtyVertexBegin = firstVertex;
    }
    mImpl->dirtyVertexEnd = firstVertex + vertexCount;
    // Record that this draw samples the bound slots' current storage, so a
    // later same-frame upload knows it must not overwrite that memory.
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture] || mImpl->selectedFramebuffers[texture] != 0) {
            continue;
        }
        const uint32_t sampledId = mImpl->selectedTextures[texture];
        if (sampledId != 0 && sampledId < mImpl->textures.size() && mImpl->textures[sampledId].initialized) {
            mImpl->textures[sampledId].lastDrawnFrame = mImpl->frameOrdinal;
        }
    }
    if (mImpl->decal) {
        // Preserve per-triangle slope bias and ordering, but submit adjacent
        // triangles with exactly equal bias together. Flat decals in particular
        // need only one state update and draw, instead of one per triangle.
        size_t batchBegin = firstVertex;
        float batchBias = 0.0f;
        const auto submitBatch = [&](size_t endVertex) {
            C3D_DepthMap(true, -1.0f, batchBias);
            C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(batchBegin),
                           static_cast<int>(endVertex - batchBegin));
            ++mImpl->drawCallCount;
            mImpl->sampleFogDrawCount += program->fog ? 1u : 0u;
        };
        for (size_t vertex = firstVertex; vertex < firstVertex + vertexCount; vertex += 3) {
            const auto* triangle = mImpl->packedVertices + vertex;
            const float bias = DecalDepthBias3DS(triangle[0].position, triangle[1].position,
                triangle[2].position, mImpl->viewportWidth, mImpl->viewportHeight);
            if (vertex != batchBegin && bias != batchBias) {
                submitBatch(vertex);
                batchBegin = vertex;
            }
            batchBias = bias;
        }
        if (batchBegin < firstVertex + vertexCount) submitBatch(firstVertex + vertexCount);
    } else {
        C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
        ++mImpl->drawCallCount;
        mImpl->sampleFogDrawCount += program->fog ? 1u : 0u;
    }
    mImpl->triangleCount += vertexCount / 3;
    mImpl->packedVertexCount += vertexCount;
    mImpl->framePeakPackedVertices = std::max(mImpl->framePeakPackedVertices, mImpl->packedVertexCount);
    mImpl->samplePeakPackedVertices =
        std::max(mImpl->samplePeakPackedVertices, mImpl->framePeakPackedVertices);
}
