// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace Vulkan::KillzoneCamera {
struct Camera {
    std::array<float,12> view{}, inverse_view{}; // positive-view-Z, row-major 3x4
    std::array<float,4> projection{};
    float far_plane{};
};

// CUSA00190 01.81, native1080p world camera observed in scene-depth and lighting
// passes. Reject the weapon projection, unrelated buffers and unsupported sizes.
inline bool DecodeWorld(std::span<const float> data, Camera& result) {
    if(data.size()!=160 || data[114]!=1920.f || data[115]!=1080.f) return false;
    for(unsigned i=0;i<48;++i) if(!std::isfinite(data[i])) return false;
    const auto v=data.subspan(0,16), p=data.subspan(16,16), inv=data.subspan(32,16);
    for(unsigned i: {3u,7u,11u}) if(std::abs(v[i])>1e-5f || std::abs(inv[i])>1e-5f) return false;
    if(std::abs(v[15]-1.f)>1e-5f || std::abs(inv[15]-1.f)>1e-5f) return false;
    for(unsigned i: {1u,2u,3u,4u,6u,7u,8u,9u,12u,13u,15u})
        if(std::abs(p[i])>1e-5f) return false;
    if(p[0]<.25f || p[5]<.25f || p[0]>10.f || p[5]>10.f ||
       std::abs(p[0]/p[5]-1080.f/1920.f)>1e-4f || std::abs(p[11]+1.f)>1e-5f)
        return false;
    const float a=-p[10], b=p[14], near_plane=-b/a;
    // The separate weapon camera uses near~0.02; world uses near~0.4.
    if(a<=1.f || a>1.01f || near_plane<.39f || near_plane>.41f) return false;
    const float far_plane=-b/(a-1.f);
    if(!std::isfinite(far_plane) || far_plane<1000.f || far_plane>1000000.f) return false;
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) {
        double value=0;
        for(unsigned k=0;k<4;++k) value+=double(v[k*4+r])*inv[c*4+k];
        if(std::abs(value-(r==c ? 1. : 0.))>.02) return false;
    }
    // S=diag(1,1,-1,1): V_lh=S*V_rh, inverse_lh=inverse_rh*S.
    // This lets the existing +Z camera-motion shader reproduce Killzone's w=-Z.
    Camera decoded;
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<4;++c) {
        decoded.view[r*4+c]=v[c*4+r]*(r==2 ? -1.f : 1.f);
        decoded.inverse_view[r*4+c]=inv[c*4+r]*(c==2 ? -1.f : 1.f);
    }
    decoded.projection={p[0],p[5],a,b};
    decoded.far_plane=far_plane;
    result=decoded;
    return true;
}
} // namespace Vulkan::KillzoneCamera
