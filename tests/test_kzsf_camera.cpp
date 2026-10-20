#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "video_core/renderer_vulkan/kzsf_camera.h"
#include "fixtures/kzsf_world_cameras.h"
using Vulkan::KillzoneCamera::Camera;
using Vulkan::KillzoneCamera::DecodeWorld;
using Point=std::array<double,4>;
static Point rh_view(const std::array<float,160>& camera,const Point& world) {
    Point result{};
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) result[r]+=camera[c*4+r]*world[c];
    return result;
}
static Point affine(const std::array<float,12>& matrix,const Point& p) {
    Point result{0,0,0,1};
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<4;++c) result[r]+=matrix[r*4+c]*p[c];
    return result;
}
int main() {
    std::array<std::array<float,160>,3> captures;
    for(unsigned i=0;i<3;++i)
        for(unsigned j=0;j<160;++j) captures[i][j]=kzsf_world_cameras[i][j];
    std::array<Camera,3> parsed;
    for(unsigned i=0;i<3;++i) {
        assert(DecodeWorld(captures[i],parsed[i]));
        assert(std::abs(-parsed[i].projection[3]/parsed[i].projection[2]-.4f)<1e-4f);
        assert(parsed[i].far_plane>30000 && parsed[i].far_plane<50000);
    }
    // Compare the unchanged shader's +Z unprojection/reprojection contract against
    // direct original -Z matrices from real camera turns of~87 and~89 degrees.
    for(unsigned i=1;i<3;++i) {
        const auto& prev=captures[i-1];const auto& curr=captures[i];
        Point world{0,0,0,1};
        for(unsigned r=0;r<3;++r)
            world[r]=(prev[44+r]+curr[44+r])/2.-100.*(prev[40+r]+curr[40+r]);
        const auto v=rh_view(curr,world),pv=rh_view(prev,world);
        assert(v[2]<0 && pv[2]<0);
        const double ndcx=curr[16]*v[0]/-v[2],ndcy=curr[21]*v[1]/-v[2];
        const double depth=-curr[26]+curr[30]/-v[2];
        const auto& c=parsed[i];
        const double z=c.projection[3]/(depth-c.projection[2]);
        const Point unprojected{ndcx*z/c.projection[0],ndcy*z/c.projection[1],z,1};
        const auto rebuilt_world=affine(c.inverse_view,unprojected);
        const auto reprojected=affine(parsed[i-1].view,rebuilt_world);
        const double actual_x=reprojected[0]*parsed[i-1].projection[0]/reprojected[2];
        const double actual_y=reprojected[1]*parsed[i-1].projection[1]/reprojected[2];
        const double expected_x=prev[16]*pv[0]/-pv[2],expected_y=prev[21]*pv[1]/-pv[2];
        assert(std::abs(actual_x-expected_x)*960<.02);
        assert(std::abs(actual_y-expected_y)*540<.02);
        // TAA expectedPreviousDepth: the positive-Z depth rejection must agree
        // with the original guest projection across captured camera turns.
        const double predicted_depth=parsed[i-1].projection[2]+
            parsed[i-1].projection[3]/reprojected[2];
        const double guest_previous_depth=-prev[26]+prev[30]/-pv[2];
        assert(std::abs(predicted_depth-guest_previous_depth)<2e-7);

    }
    auto bad=captures[0]; Camera result=parsed[0];
    bad[16]=1.73205101f;bad[21]=3.07920194f;bad[26]=-1.0000006f;bad[30]=-.02f;
    assert(!DecodeWorld(bad,result)); // weapon projection must never replace world camera
    assert(result.view==parsed[0].view && result.projection==parsed[0].projection);
    bad=captures[0];bad[44]+=10;assert(!DecodeWorld(bad,result));
    bad=captures[0];bad[114]=3840;assert(!DecodeWorld(bad,result));
    bad=captures[0];bad[0]=std::numeric_limits<float>::quiet_NaN();assert(!DecodeWorld(bad,result));
    assert(!DecodeWorld(std::span<const float>(captures[0].data(),159),result));
    std::puts("PASS: three real cameras, signed-Z/depth reprojection within0.02 pixels, weapon/bad-data rejection");
}
