// Immutable dirty-bd091259 oracle excerpts; retain original bodies verbatim.
// Source: third_party/libultraship/src/fast/interpreter.cpp
// Full source SHA-256: 3667aef9ad14443ce1f4fca8f604277672cbac245a17a9be300ae262a347348f

bool Interpreter::TryEmitTriangleArm11(struct LoadedVertex* const v_arr[3], bool is_rect) {
    const TriStateCache& ts = mTriState;
    // Stock OoT SetupDL25 and common title layouts use standard fog and two
    // shader UV sets. RGB and alpha inputs are numbered independently.
    if (ts.use_fog && !ts.use_blend_color && !ts.use_grayscale &&
        ts.tm == 0 && ts.usedTextures[0] && ts.usedTextures[1] && ts.numInputs == 2 &&
        ts.comb->shader_input_mapping[0][0] == G_CCMUX_SHADE &&
        (ts.comb->shader_input_mapping[0][1] == G_CCMUX_PRIMITIVE ||
         ts.comb->shader_input_mapping[0][1] == G_CCMUX_ENVIRONMENT)) {
        static_assert(sizeof(Soh3dsFogEmitParams) == 84 &&
                      offsetof(Soh3dsFogEmitParams, texture) == 4 &&
                      sizeof(Soh3dsFogEmitParams::Texture) == 24 &&
                      offsetof(Soh3dsFogEmitParams, fog) == 52 &&
                      offsetof(Soh3dsFogEmitParams, uniform) == 64 &&
                      offsetof(Soh3dsFogEmitParams, alpha) == 76);
        Soh3dsFogEmitParams params;
        params.flags = (ts.use_alpha ? 1u : 0u) | (mClipParameters.invertY ? 4u : 0u) |
                       (mClipParameters.z_is_from_0_to_1 ? 8u : 0u) | (is_rect ? 16u : 0u);
        constexpr float kInv255 = 1.0f / 255.0f;
        if (ts.use_alpha) {
            for (unsigned input = 0; input < 2; ++input) {
                uint8_t alpha;
                switch (ts.comb->shader_input_mapping[1][input]) {
                    case 0: alpha = 0; break;
                    case G_CCMUX_SHADE: params.alpha[input] = 1.0f; continue; // Standard fog forces shade alpha to 1.
                    case G_CCMUX_PRIMITIVE: alpha = mRdp->prim_color.a; break;
                    case G_CCMUX_ENVIRONMENT: alpha = mRdp->env_color.a; break;
                    case G_CCMUX_KEY_CENTER: alpha = mRdp->key_center.a; break;
                    case G_CCMUX_KEY_SCALE: alpha = mRdp->key_scale.a; break;
                    default: return false;
                }
                params.alpha[input] = alpha * kInv255;
            }
        }
        for (unsigned t = 0; t < 2; ++t) {
            params.texture[t] = { ts.uMul[t], ts.vMul[t], ts.uAdd[t], ts.vAdd[t],
                                  ts.halfU[t], ts.halfV[t] };
        }
        params.fog[0] = mRdp->fog_color.r * kInv255;
        params.fog[1] = mRdp->fog_color.g * kInv255;
        params.fog[2] = mRdp->fog_color.b * kInv255;
        const RGBA& uniform = ts.comb->shader_input_mapping[0][1] == G_CCMUX_PRIMITIVE
                                  ? mRdp->prim_color : mRdp->env_color;
        params.uniform[0] = uniform.r * kInv255;
        params.uniform[1] = uniform.g * kInv255;
        params.uniform[2] = uniform.b * kInv255;
        const void* vertices[3] = { v_arr[0], v_arr[1], v_arr[2] };
        mBufVboLen = Soh3dsEmitFogTriangleArm11(mBufVbo + mBufVboLen, vertices, &params) - mBufVbo;
        return true;
    }
    // These common shader layouts need no per-vertex source selection. Keep
    // uncommon mapping/channel combinations on the exact generic path.
    if (!ts.use_alpha || ts.use_fog || ts.use_grayscale || ts.usedTextures[1] || ts.tm != 0 ||
        (ts.numInputs != 1 && ts.numInputs != 2) ||
        ts.comb->shader_input_mapping[0][0] != G_CCMUX_SHADE ||
        ts.comb->shader_input_mapping[1][0] != G_CCMUX_SHADE) {
        return false;
    }
    const RGBA* uniform = nullptr;
    if (ts.numInputs == 2) {
        const uint8_t mapping = ts.comb->shader_input_mapping[0][1];
        if (mapping != ts.comb->shader_input_mapping[1][1]) {
            return false;
        }
        if (mapping == G_CCMUX_PRIMITIVE) {
            uniform = &mRdp->prim_color;
        } else if (mapping == G_CCMUX_ENVIRONMENT) {
            uniform = &mRdp->env_color;
        } else {
            return false;
        }
    }
    static_assert(sizeof(LoadedVertex) == 32 && offsetof(LoadedVertex, x) == 0 &&
                  offsetof(LoadedVertex, y) == 4 && offsetof(LoadedVertex, z) == 8 &&
                  offsetof(LoadedVertex, w) == 12 && offsetof(LoadedVertex, u) == 16 &&
                  offsetof(LoadedVertex, v) == 20 && offsetof(LoadedVertex, color) == 24 &&
                  sizeof(RGBA) == 4 && offsetof(RGBA, r) == 0 && offsetof(RGBA, g) == 1 &&
                  offsetof(RGBA, b) == 2 && offsetof(RGBA, a) == 3);
    static_assert(sizeof(Soh3dsTriangleEmitParams) == 44 &&
                  offsetof(Soh3dsTriangleEmitParams, uMul) == 4 &&
                  offsetof(Soh3dsTriangleEmitParams, halfV) == 24 &&
                  offsetof(Soh3dsTriangleEmitParams, uniform) == 28);
    Soh3dsTriangleEmitParams params;
    params.flags = (ts.usedTextures[0] ? 1u : 0u) | (uniform ? 2u : 0u) |
                   (mClipParameters.invertY ? 4u : 0u) |
                   (mClipParameters.z_is_from_0_to_1 ? 8u : 0u) | (is_rect ? 16u : 0u);
    if (ts.usedTextures[0]) {
        params.uMul = ts.uMul[0]; params.vMul = ts.vMul[0];
        params.uAdd = ts.uAdd[0]; params.vAdd = ts.vAdd[0];
        params.halfU = ts.halfU[0]; params.halfV = ts.halfV[0];
    }
    if (uniform) {
        constexpr float kInv255 = 1.0f / 255.0f;
        params.uniform[0] = uniform->r * kInv255;
        params.uniform[1] = uniform->g * kInv255;
        params.uniform[2] = uniform->b * kInv255;
        params.uniform[3] = uniform->a * kInv255;
    }
    const void* vertices[3] = { v_arr[0], v_arr[1], v_arr[2] };
    mBufVboLen = Soh3dsEmitTriangleArm11(mBufVbo + mBufVboLen, vertices, &params) - mBufVbo;
    return true;
}

void Interpreter::EmitTriangle(struct LoadedVertex* const v_arr[3], bool is_rect) {
    // Includes a capacity-triggered Flush and its nested backend work.
    Soh3dsProfileScope profile(Soh3dsProfileSection::TriangleEmit);
#if defined(SOH3DS_ARM11_TRIANGLE_EMIT) && SOH3DS_ARM11_TRIANGLE_EMIT
    if (TryEmitTriangleArm11(v_arr, is_rect)) {
        if (++mBufVboNumTris == MAX_TRI_BUFFER) {
            Flush();
        }
        return;
    }
#endif
    const TriStateCache& ts = mTriState;
    ColorCombiner* comb = ts.comb;
    const uint8_t numInputs = ts.numInputs;
    const bool* usedTextures = ts.usedTextures;
    const bool use_alpha = ts.use_alpha;
    const bool use_fog = ts.use_fog;
    const bool use_blend_color = ts.use_blend_color;
    const bool use_grayscale = ts.use_grayscale;
    const uint32_t tm = ts.tm;
    const struct GfxClipParameters& clip_parameters = mClipParameters;
    struct LoadedVertex* v1 = v_arr[0];


    for (int i = 0; i < 3; i++) {
        float z = v_arr[i]->z, w = v_arr[i]->w;
        if (clip_parameters.z_is_from_0_to_1) {
            z = (z + w) * 0.5f;
        }

        mBufVbo[mBufVboLen++] = v_arr[i]->x;
        mBufVbo[mBufVboLen++] = clip_parameters.invertY ? -v_arr[i]->y : v_arr[i]->y;
        mBufVbo[mBufVboLen++] = z;
        mBufVbo[mBufVboLen++] = w;

        for (int t = 0; t < 2; t++) {
            if (!usedTextures[t]) {
                continue;
            }
            float u = v_arr[i]->u * ts.uMul[t] + ts.uAdd[t];
            float v = v_arr[i]->v * ts.vMul[t] + ts.vAdd[t];
            if (!is_rect) {
                // Linear filter adds 0.5f to the coordinates
                u += ts.halfU[t];
                v += ts.halfV[t];
            }
            mBufVbo[mBufVboLen++] = u;
            mBufVbo[mBufVboLen++] = v;

            if (tm & (1 << 2 * t)) {
                mBufVbo[mBufVboLen++] = ts.clampS[t];
            }
            if (tm & (1 << 2 * t + 1)) {
                mBufVbo[mBufVboLen++] = ts.clampT[t];
            }
        }

        constexpr float kInv255 = 1.0f / 255.0f;
        if (use_fog) {
            if (use_blend_color) {
                // Shroud/blend mode: blend toward blend_color using fog alpha as factor
                mBufVbo[mBufVboLen++] = mRdp->blend_color.r * kInv255;
                mBufVbo[mBufVboLen++] = mRdp->blend_color.g * kInv255;
                mBufVbo[mBufVboLen++] = mRdp->blend_color.b * kInv255;
                mBufVbo[mBufVboLen++] = mRdp->fog_color.a * kInv255;
            } else {
                mBufVbo[mBufVboLen++] = mRdp->fog_color.r * kInv255;
                mBufVbo[mBufVboLen++] = mRdp->fog_color.g * kInv255;
                mBufVbo[mBufVboLen++] = mRdp->fog_color.b * kInv255;
                mBufVbo[mBufVboLen++] = v_arr[i]->color.a * kInv255; // fog factor (not alpha)
            }
        }

        if (use_grayscale) {
            mBufVbo[mBufVboLen++] = mRdp->grayscale_color.r * kInv255;
            mBufVbo[mBufVboLen++] = mRdp->grayscale_color.g * kInv255;
            mBufVbo[mBufVboLen++] = mRdp->grayscale_color.b * kInv255;
            mBufVbo[mBufVboLen++] = mRdp->grayscale_color.a * kInv255; // lerp interpolation factor (not alpha)
        }

        for (int j = 0; j < numInputs; j++) {
            RGBA* color;
            RGBA tmp;
            for (int k = 0; k < 1 + (use_alpha ? 1 : 0); k++) {
                switch (comb->shader_input_mapping[k][j]) {
                        // Note: CCMUX constants and ACMUX constants used here have same value, which is why this works
                        // (except LOD fraction).
                    case G_CCMUX_PRIMITIVE:
                        color = &mRdp->prim_color;
                        break;
                    case G_CCMUX_SHADE:
                        color = &v_arr[i]->color;
                        break;
                    case G_CCMUX_ENVIRONMENT:
                        color = &mRdp->env_color;
                        break;
                    case G_CCMUX_PRIMITIVE_ALPHA: {
                        tmp.r = tmp.g = tmp.b = mRdp->prim_color.a;
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_ENV_ALPHA: {
                        tmp.r = tmp.g = tmp.b = mRdp->env_color.a;
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_PRIM_LOD_FRAC: {
                        tmp.r = tmp.g = tmp.b = mRdp->prim_lod_fraction;
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_LOD_FRACTION: {
                        if (mRdp->other_mode_l & G_TL_LOD) {
                            // "Hack" that works for Bowser - Peach painting
                            float distance_frac = (v1->w - 3000.0f) / 3000.0f;
                            if (distance_frac < 0.0f) {
                                distance_frac = 0.0f;
                            }
                            if (distance_frac > 1.0f) {
                                distance_frac = 1.0f;
                            }
                            tmp.r = tmp.g = tmp.b = tmp.a = distance_frac * 255.0f;
                        } else {
                            tmp.r = tmp.g = tmp.b = tmp.a = 255.0f;
                        }
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_KEY_CENTER:
                        color = &mRdp->key_center;
                        break;
                    case G_CCMUX_KEY_SCALE:
                        color = &mRdp->key_scale;
                        break;
                    case G_CCMUX_CONVERT_K4: {
                        tmp.r = tmp.g = tmp.b = mRdp->convert_k[4];
                        color = &tmp;
                        break;
                    }
                    case G_CCMUX_CONVERT_K5: {
                        tmp.r = tmp.g = tmp.b = mRdp->convert_k[5];
                        color = &tmp;
                        break;
                    }
                    case G_ACMUX_PRIM_LOD_FRAC:
                        tmp.a = mRdp->prim_lod_fraction;
                        color = &tmp;
                        break;
                    default:
                        memset(&tmp, 0, sizeof(tmp));
                        color = &tmp;
                        break;
                }
                if (k == 0) {
                    mBufVbo[mBufVboLen++] = color->r * kInv255;
                    mBufVbo[mBufVboLen++] = color->g * kInv255;
                    mBufVbo[mBufVboLen++] = color->b * kInv255;
                } else {
                    if (use_fog && !use_blend_color && color == &v_arr[i]->color) {
                        // Shade alpha is 100% for standard fog, blend color mode preserves
                        // it since fog alpha is the blend factor
                        mBufVbo[mBufVboLen++] = 1.0f;
                    } else {
                        mBufVbo[mBufVboLen++] = color->a * kInv255;
                    }
                }
            }
        }

        // struct RGBA *color = &v_arr[i]->color;
        // mBufVbo[mBufVboLen++] = color->r / 255.0f;
        // mBufVbo[mBufVboLen++] = color->g / 255.0f;
        // mBufVbo[mBufVboLen++] = color->b / 255.0f;
        // mBufVbo[mBufVboLen++] = color->a / 255.0f;
    }

    if (++mBufVboNumTris == MAX_TRI_BUFFER) {
        // if (++mBufVbo_num_tris == 1) {
        Flush();
    }
}

void Interpreter::Flush() {
    if (mBufVboLen > 0) {
        mRapi->SetCurrentPrimDepth((float)mRdp->prim_depth / N64_PRIM_DEPTH_MAX);
        mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris);
        mBufVboLen = 0;
        mBufVboNumTris = 0;
    }
}
