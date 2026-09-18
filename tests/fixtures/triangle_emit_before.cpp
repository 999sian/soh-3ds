void Interpreter::EmitTriangle(struct LoadedVertex* const v_arr[3], bool is_rect) {
    // Includes a capacity-triggered Flush and its nested backend work.
    Soh3dsProfileScope profile(Soh3dsProfileSection::TriangleEmit);
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
