// Immutable pre-batch production triangle/dispatch methods, build d773e404.
void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {
    GfxSpTri1Impl(vtx1_idx, vtx2_idx, vtx3_idx, is_rect, false);
}

bool Interpreter::GfxSpTri1Impl(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect, bool reuse) {
    Soh3dsProfileScope triangleProfile(Soh3dsProfileSection::Triangle);
    struct LoadedVertex* v1 = &mRsp->loaded_vertices[vtx1_idx];
    struct LoadedVertex* v2 = &mRsp->loaded_vertices[vtx2_idx];
    struct LoadedVertex* v3 = &mRsp->loaded_vertices[vtx3_idx];
    struct LoadedVertex* v_arr[3] = { v1, v2, v3 };

    // if (rand()%2) return;

    if (v1->clip_rej & v2->clip_rej & v3->clip_rej) {
        // The whole triangle lies outside the visible area
        return false;
    }

    const uint32_t cull_both = get_attr(CULL_BOTH);
    const uint32_t cull_front = get_attr(CULL_FRONT);
    const uint32_t cull_back = get_attr(CULL_BACK);

    if ((mRsp->geometry_mode & cull_both) != 0) {
        // SoH-3DS: sign-identical to the projected screen-space cross product
        // without six VFP divides (~19 non-pipelined cycles each on ARM11):
        // cross = N / (w1*w2^2*w3) and w2^2 > 0, so sign(cross) = sign(N*w1*w3).
        const float ex1 = v1->x * v2->w - v2->x * v1->w;
        const float ey1 = v1->y * v2->w - v2->y * v1->w;
        const float ex2 = v3->x * v2->w - v2->x * v3->w;
        const float ey2 = v3->y * v2->w - v2->y * v3->w;
        float cross = (ex1 * ey2 - ey1 * ex2) * (v1->w * v3->w);

        if ((v1->w < 0) ^ (v2->w < 0) ^ (v3->w < 0)) {
            // If one vertex lies behind the eye, negating cross will give the correct result.
            // If all vertices lie behind the eye, the triangle will be rejected anyway.
            cross = -cross;
        }

        // G_EX_INVERT_CULLING is a LUS extension, not tied to a specific ucode,
        // so apply it regardless of the active microcode handler.
        if ((mRsp->extra_geometry_mode & G_EX_INVERT_CULLING) != 0) {
            cross = -cross;
        }

        auto cull_type = mRsp->geometry_mode & cull_both;

        if (cull_type == cull_front) {
            if (cross <= 0) {
                return false;
            }
        } else if (cull_type == cull_back) {
            if (cross >= 0) {
                return false;
            }
        } else if (cull_type == cull_both) {
            // Why is this even an option?
            return false;
        }
    }

    const bool viewportWork = mRdp->viewport_or_scissor_changed;
    if (viewportWork) {
        reuse = false;
        if (memcmp(&mRdp->viewport, &mRenderingState.viewport, sizeof(mRdp->viewport)) != 0) {
            Flush();
            mRapi->SetViewport(mRdp->viewport.x, mRdp->viewport.y, mRdp->viewport.width, mRdp->viewport.height);
            mRenderingState.viewport = mRdp->viewport;
        }
        if (memcmp(&mRdp->scissor, &mRenderingState.scissor, sizeof(mRdp->scissor)) != 0) {
            Flush();
            mRapi->SetScissor(mRdp->scissor.x, mRdp->scissor.y, mRdp->scissor.width, mRdp->scissor.height);
            mRenderingState.scissor = mRdp->scissor;
        }
        mRdp->viewport_or_scissor_changed = false;
    }

    bool needsDerive = false;
#if SOH3DS_TRIANGLE_PAIR_REUSE
    if (!reuse)
#else
    (void)reuse;
#endif
    {
        Soh3dsProfileScope profile(Soh3dsProfileSection::TriangleKey);
        TriStateKey key;
        CaptureTriStateKey(&key);
        needsDerive = !mTriState.valid || !(mTriState.key == key);
    }
    if (needsDerive) {
        DeriveTriState();
        // Re-capture: the derivation clears textures_changed and can rebind
        // textures or repair loaded dimensions, so the key must describe the post-state.
        {
            Soh3dsProfileScope profile(Soh3dsProfileSection::TriangleKey);
            CaptureTriStateKey(&mTriState.key);
        }
        mTriState.valid = true;
    }
#if SOH3DS_TRIANGLE_PAIR_REUSE
    const bool capacityFlush = mBufVboNumTris + 1 == MAX_TRI_BUFFER;
#endif
    EmitTriangle(v_arr, is_rect);
#if SOH3DS_TRIANGLE_PAIR_REUSE
    // Only a completed equality hit with no emission callback licenses reuse.
    return !needsDerive && !capacityFlush && !viewportWork;
#else
    return false;
#endif
}

void Interpreter::CaptureTriStateKey(TriStateKey* key) {
    key->combine_mode = mRdp->combine_mode;
    key->other_mode_l = mRdp->other_mode_l;
    key->other_mode_h = mRdp->other_mode_h;
    key->geometry_mode = mRsp->geometry_mode;
    key->extra_geometry_mode = mRsp->extra_geometry_mode;
    key->shader_id = mShaderStack.empty() ? -1 : (int32_t)mShaderStack.top();
    key->shaderProgram = mRenderingState.mShaderProgram;
    key->first_tile_index = mRdp->first_tile_index;
    key->depth_test_and_mask = mRenderingState.depth_test_and_mask;
    key->grayscale = mRdp->grayscale;
    key->decal_mode = mRenderingState.decal_mode;
    key->alpha_blend = mRenderingState.alpha_blend;
    for (int i = 0; i < 2; i++) {
        uint32_t tile = mRdp->first_tile_index + i;
        if (i == 1 && mRdp->first_tile_index >= 2) {
            tile = mRdp->first_tile_index;
        }
        const auto& src = mRdp->texture_tile[tile];
        auto& dst = key->tile[i];
        dst.uls = src.uls;
        dst.ult = src.ult;
        dst.lrs = src.lrs;
        dst.lrt = src.lrt;
        dst.line_size_bytes = src.line_size_bytes;
        dst.siz = src.siz;
        dst.cms = src.cms;
        dst.cmt = src.cmt;
        dst.masks = src.masks;
        dst.maskt = src.maskt;
        dst.shifts = src.shifts;
        dst.shiftt = src.shiftt;
        dst.tmem_index = src.tmem_index;
        const auto& loaded = mRdp->loaded_texture[src.tmem_index];
        key->loaded[i].orig_size_bytes = loaded.orig_size_bytes;
        key->loaded[i].size_bytes = loaded.size_bytes;
        key->loaded[i].full_image_line_size_bytes = loaded.full_image_line_size_bytes;
        key->loaded[i].line_size_bytes = loaded.line_size_bytes;
        key->loaded[i].h_byte_scale = loaded.raw_tex_metadata.h_byte_scale;
        key->loaded[i].v_pixel_scale = loaded.raw_tex_metadata.v_pixel_scale;
        key->textures[i] = mRenderingState.mTextures[i];
        key->masked[i] = mRdp->loaded_texture[i].masked;
        key->blended[i] = mRdp->loaded_texture[i].blended;
        key->textures_changed[i] = mRdp->textures_changed[i];
    }
}

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

void Interpreter::Flush() {
    if (mBufVboLen > 0) {
        mRapi->SetCurrentPrimDepth((float)mRdp->prim_depth / N64_PRIM_DEPTH_MAX);
        mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris);
        mBufVboLen = 0;
        mBufVboNumTris = 0;
    }
}

bool gfx_tri2_handler_f3dex(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;
    F3DGfx* cmd = *cmd0;

#if SOH3DS_TRIANGLE_PAIR_REUSE
    // The token is local to this command; decode triangle two after triangle
    // one's possible derivation/flush callbacks, just as the generic path does.
    const bool reuse = gfx->GfxSpTri1Impl(C0(17, 7), C0(9, 7), C0(1, 7), false, false);
    gfx->GfxSpTri1Impl(C1(17, 7), C1(9, 7), C1(1, 7), false, reuse);
#else
    gfx->GfxSpTri1(C0(17, 7), C0(9, 7), C0(1, 7), false);
    gfx->GfxSpTri1(C1(17, 7), C1(9, 7), C1(1, 7), false);
#endif
    return false;
}

bool gfx_quad_handler_f3dex2(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;
    F3DGfx* cmd = *cmd0;

#if SOH3DS_TRIANGLE_PAIR_REUSE
    const bool reuse = gfx->GfxSpTri1Impl(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2, false, false);
    gfx->GfxSpTri1Impl(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2, false, reuse);
#else
    gfx->GfxSpTri1(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2, false);
    gfx->GfxSpTri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2, false);
#endif
    return false;
}

bool gfx_quad_handler_f3dex(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;
    F3DGfx* cmd = *cmd0;

#if SOH3DS_TRIANGLE_PAIR_REUSE
    const bool reuse = gfx->GfxSpTri1Impl(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2, false, false);
    gfx->GfxSpTri1Impl(C1(16, 8) / 2, C1(0, 8) / 2, C1(24, 8) / 2, false, reuse);
#else
    gfx->GfxSpTri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2, false);
    gfx->GfxSpTri1(C1(16, 8) / 2, C1(0, 8) / 2, C1(24, 8) / 2, false);
#endif
    return false;
}

bool gfx_tri1_otr_handler_f3dex2(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;

    F3DGfx* cmd = *cmd0;
    uint8_t v00 = (uint8_t)(cmd->words.w0 & 0x0000FFFF);
    uint8_t v01 = (uint8_t)(cmd->words.w1 >> 16);
    uint8_t v02 = (uint8_t)(cmd->words.w1 & 0x0000FFFF);
    gfx->GfxSpTri1(v00, v01, v02, false);

    return false;
}

bool gfx_tri1_handler_f3dex2(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;
    F3DGfx* cmd = *cmd0;

    gfx->GfxSpTri1(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2, false);

    return false;
}

bool gfx_tri1_handler_f3dex(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;
    F3DGfx* cmd = *cmd0;

    gfx->GfxSpTri1(C1(17, 7), C1(9, 7), C1(1, 7), false);

    return false;
}

bool gfx_tri1_handler_f3d(F3DGfx** cmd0) {
    Interpreter* gfx = sInterpreterRaw;
    F3DGfx* cmd = *cmd0;

    gfx->GfxSpTri1(C1(16, 8) / 10, C1(8, 8) / 10, C1(0, 8) / 10, false);

    return false;
}

static void gfx_step() {
    auto& cmd = g_exec_stack.currCmd();
    auto cmd0 = cmd;
    int8_t opcode = (int8_t)(cmd->words.w0 >> 24);

#ifdef USE_GBI_TRACE
    if (cmd->words.trace.valid &&
        Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger("gEnableGFXTrace", 0)) {
#define TRACE                                  \
    "\n====================================\n" \
    " - CMD: {:02X}\n"                         \
    " - Path: {}:{}\n"                         \
    " - W0: {:08X}\n"                          \
    " - W1: {:08X}\n"                          \
    "===================================="
        SPDLOG_INFO(TRACE, (uint8_t)opcode, cmd->words.trace.file, cmd->words.trace.idx, cmd->words.w0, cmd->words.w1);
    }
#endif

    if (opcode == F3DEX2_G_LOAD_UCODE) {
        gfx_set_ucode_handler((UcodeHandlers)(cmd->words.w0 & 0xFFFFFF));
        ++cmd;
        return;
        // Instead of having a handler for each ucode for switching ucode, just check for it early and return.
    }

    if (otrHandlers.contains(opcode)) {
        // OTR filepath handlers expect w1 to be a valid string pointer.
        // Guard against null or N64-segment addresses that would crash in strlen/strncmp.
        if (opcode == OTR_G_VTX_OTR_FILEPATH || opcode == OTR_G_SETTIMG_OTR_FILEPATH ||
            opcode == OTR_G_DL_OTR_FILEPATH || opcode == OTR_G_PUSHCD || opcode == OTR_G_MTX_OTR_FILEPATH) {
            uintptr_t w1 = (uintptr_t)cmd->words.w1;
            if (w1 < 0x10000
#if UINTPTR_MAX > 0xFFFFFFFFu
                // On 64-bit: filter kernel/sentinel addresses.
                || w1 > 0x0000FFFFFFFFFFFFull
#endif
            ) {
                ++g_exec_stack.currCmd();
                return;
            }
        }
        if (otrHandlers.at(opcode).second(&cmd)) {
            return;
        }
    } else if (rdpHandlers.contains(opcode)) {
        if (rdpHandlers.at(opcode).second(&cmd)) {
            return;
        }
    } else if (ucode_handler_index < ucode_handlers.size()) {
        if (ucode_handlers[ucode_handler_index]->contains(opcode)) {
            if (ucode_handlers[ucode_handler_index]->at(opcode).second(&cmd)) {
                return;
            }
        } else {
            SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, for loaded ucode: {}", (uint8_t)opcode,
                            (uint32_t)ucode_handler_index);
        }
    } else {
        SPDLOG_CRITICAL("Unhandled OP code: 0x{:X}, invalid ucode: {}", (uint8_t)opcode, (uint32_t)ucode_handler_index);
    }

    ++cmd;
}
