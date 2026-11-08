// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// deko3d setup follows the nanovg-deko3d example and Sphaira's app.cpp.
#include "gfx.hpp"

#include <array>
#include <nanovg_dk.h> // defines static functions: include it in this file only

namespace nxdock {
namespace {

void FramebufferSize(int& w, int& h) {
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        w = 1920; h = 1080;
    } else {
        w = 1280; h = 720;
    }
}

} // namespace

bool Gfx::Init() {
    FramebufferSize(m_width, m_height);
    m_scale = m_width / kWidth;

    m_device = dk::DeviceMaker{}.create();
    m_queue = dk::QueueMaker{m_device}.setFlags(DkQueueFlags_Graphics).create();

    m_pool_images.emplace(m_device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16 * 1024 * 1024);
    m_pool_code.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
    m_pool_data.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

    m_cmdbuf = dk::CmdBufMaker{m_device}.create();
    const CMemPool::Handle cmdmem = m_pool_data->allocate(kStaticCmdSize);
    m_cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

    CreateFramebuffers();

    m_renderer.emplace(m_width, m_height, m_device, m_queue, *m_pool_images, *m_pool_code, *m_pool_data);
    m_vg = nvgCreateDk(&*m_renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!m_vg) {
        return false;
    }

    LoadFonts();
    return true;
}

void Gfx::Exit() {
    if (m_vg) {
        nvgDeleteDk(m_vg);
        m_vg = nullptr;
    }
    m_renderer.reset();
    DestroyFramebuffers();
    m_cmdbuf = nullptr;
    m_pool_data.reset();
    m_pool_code.reset();
    m_pool_images.reset();
    m_queue = nullptr;
    m_device = nullptr;
    plExit();
}

void Gfx::LoadFonts() {
    // The console's own fonts: nothing to ship, and every language is covered.
    if (R_FAILED(plInitialize(PlServiceType_User))) {
        return;
    }
    PlFontData standard{}, extended{};
    plGetSharedFontByType(&standard, PlSharedFontType_Standard);
    plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt);
    const int std_id = nvgCreateFontMem(m_vg, "ui", static_cast<unsigned char*>(standard.address), standard.size, 0);
    const int ext_id = nvgCreateFontMem(m_vg, "ext", static_cast<unsigned char*>(extended.address), extended.size, 0);
    nvgAddFallbackFontId(m_vg, std_id, ext_id);

    for (auto type : {PlSharedFontType_ChineseSimplified, PlSharedFontType_ExtChineseSimplified,
                      PlSharedFontType_ChineseTraditional, PlSharedFontType_KO}) {
        PlFontData lang{};
        if (R_SUCCEEDED(plGetSharedFontByType(&lang, type))) {
            char name[16];
            std::snprintf(name, sizeof(name), "lang%d", static_cast<int>(type));
            const int id = nvgCreateFontMem(m_vg, name, static_cast<unsigned char*>(lang.address), lang.size, 0);
            nvgAddFallbackFontId(m_vg, std_id, id);
        }
    }
}

void Gfx::CreateFramebuffers() {
    dk::ImageLayout depth_layout;
    dk::ImageLayoutMaker{m_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(m_width, m_height)
        .initialize(depth_layout);
    m_depth_mem = m_pool_images->allocate(depth_layout.getSize(), depth_layout.getAlignment());
    m_depth.initialize(depth_layout, m_depth_mem.getMemBlock(), m_depth_mem.getOffset());

    dk::ImageLayout fb_layout;
    dk::ImageLayoutMaker{m_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(m_width, m_height)
        .initialize(fb_layout);

    std::array<DkImage const*, kNumFramebuffers> fb_array{};
    for (unsigned i = 0; i < kNumFramebuffers; i++) {
        m_fb_mem[i] = m_pool_images->allocate(fb_layout.getSize(), fb_layout.getAlignment());
        m_fb[i].initialize(fb_layout, m_fb_mem[i].getMemBlock(), m_fb_mem[i].getOffset());
        dk::ImageView color{m_fb[i]}, depth{m_depth};
        m_cmdbuf.bindRenderTargets(&color, &depth);
        m_fb_cmdlists[i] = m_cmdbuf.finishList();
        fb_array[i] = &m_fb[i];
    }

    m_swapchain = dk::SwapchainMaker{m_device, nwindowGetDefault(), fb_array}.create();
    RecordStaticCommands();
}

void Gfx::DestroyFramebuffers() {
    if (!m_swapchain) {
        return;
    }
    m_queue.waitIdle();
    m_cmdbuf.clear();
    m_swapchain = nullptr;
    for (auto& mem : m_fb_mem) {
        mem.destroy();
    }
    m_depth_mem.destroy();
}

void Gfx::RecordStaticCommands() {
    dk::RasterizerState rasterizer;
    dk::ColorState color;
    dk::ColorWriteState color_write;
    m_cmdbuf.setViewports(0, {{0.f, 0.f, float(m_width), float(m_height), 0.f, 1.f}});
    m_cmdbuf.setScissors(0, {{0, 0, u32(m_width), u32(m_height)}});
    m_cmdbuf.clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 1.f);
    m_cmdbuf.clearDepthStencil(true, 1.f, 0xFF, 0);
    m_cmdbuf.bindRasterizerState(rasterizer);
    m_cmdbuf.bindColorState(color);
    m_cmdbuf.bindColorWriteState(color_write);
    m_render_cmdlist = m_cmdbuf.finishList();
}

void Gfx::CheckResize() {
    int w, h;
    FramebufferSize(w, h);
    if (w == m_width && h == m_height) {
        return;
    }
    m_width = w;
    m_height = h;
    m_scale = m_width / kWidth;
    DestroyFramebuffers();
    CreateFramebuffers();
    m_renderer->UpdateViewSize(m_width, m_height);
}

NVGcontext* Gfx::BeginFrame() {
    m_slot = m_queue.acquireImage(m_swapchain);
    m_queue.submitCommands(m_fb_cmdlists[m_slot]);
    m_queue.submitCommands(m_render_cmdlist);
    nvgBeginFrame(m_vg, m_width, m_height, 1.f);
    nvgScale(m_vg, m_scale, m_scale);
    return m_vg;
}

void Gfx::EndFrame() {
    nvgResetTransform(m_vg);
    nvgEndFrame(m_vg);
    m_queue.presentImage(m_swapchain, m_slot);
}

} // namespace nxdock
