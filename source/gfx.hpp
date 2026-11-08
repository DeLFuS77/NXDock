// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <deko3d.hpp>
#include <nanovg.h>
#include <nanovg/dk_renderer.hpp>
#include <optional>

namespace nxdock {

// The whole interface is laid out on a 1280x720 canvas; docked, it is scaled to 1080p.
inline constexpr float kWidth = 1280.f;
inline constexpr float kHeight = 720.f;

class Gfx {
public:
    bool Init();
    void Exit();

    // Recreates the framebuffers when the console is docked or undocked.
    void CheckResize();

    NVGcontext* BeginFrame();
    void EndFrame();

    NVGcontext* vg() const { return m_vg; }

private:
    static constexpr unsigned kNumFramebuffers = 2;
    static constexpr unsigned kStaticCmdSize = 0x1000;

    void CreateFramebuffers();
    void DestroyFramebuffers();
    void RecordStaticCommands();
    void LoadFonts();

    int m_width{1280}, m_height{720};
    float m_scale{1.f};
    int m_slot{-1};

    dk::UniqueDevice m_device;
    dk::UniqueQueue m_queue;
    std::optional<CMemPool> m_pool_images, m_pool_code, m_pool_data;
    dk::UniqueCmdBuf m_cmdbuf;
    CMemPool::Handle m_depth_mem;
    CMemPool::Handle m_fb_mem[kNumFramebuffers];
    dk::Image m_depth;
    dk::Image m_fb[kNumFramebuffers];
    DkCmdList m_fb_cmdlists[kNumFramebuffers]{};
    DkCmdList m_render_cmdlist{};
    dk::UniqueSwapchain m_swapchain;
    std::optional<nvg::DkRenderer> m_renderer;
    NVGcontext* m_vg{};
};

} // namespace nxdock
