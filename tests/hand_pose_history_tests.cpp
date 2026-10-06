#include "hand_pose_history.h"
#include <cstdio>
#include <cstdlib>

using namespace w3vr::motion;
namespace {
void require(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1); }
}
Frame sample(XrTime time) {
    Frame f;
    f.display_time=time;f.tracking_epoch=1;
    f.reference_space=reinterpret_cast<XrSpace>(uintptr_t{3});
    f.ready=f.focused=true;
    for (auto& hand:f.hands) hand.grip_tracked=true;
    return f;
}
}
int main() {
    HandPoseHistory history;
    auto f=sample(1000000000);
    require(!history.update(f).hands[1].continuous,"first valid pose only seeds history");
    f.display_time+=16000000;f.hands[1].grip.position.x=0.2f;
    auto paths=history.update(f);
    require(paths.hands[1].continuous && std::abs(paths.hands[1].seconds-0.016)<1e-10 &&
            paths.hands[1].previous.position.x==0 && paths.hands[1].current.position.x>0.19,
            "consecutive tracked poses publish the right grip path in metres");
    require(!history.update(f).hands[1].continuous,"duplicate display time cannot create another sweep");
    f.display_time+=16000000;f.hands[1].grip_tracked=false;
    paths=history.update(f);
    require(!paths.hands[1].tracked && !paths.hands[1].continuous && paths.hands[0].continuous,
            "one lost hand cannot keep its old motion; other hand remains independent");
    f.display_time+=16000000;f.hands[1].grip_tracked=true;
    require(!history.update(f).hands[1].continuous,"tracking recovery seeds without crossing the gap");
    f.display_time+=16000000;++f.tracking_epoch;
    require(!history.update(f).hands[0].continuous,"new session/reference epoch cannot join old poses");
    f.display_time+=16000000;f.reference_space=reinterpret_cast<XrSpace>(uintptr_t{4});
    require(!history.update(f).hands[1].continuous,"changed reference handle starts a new path");
    f.display_time+=16000000;f.focused=false;
    require(!history.update(f).hands[1].tracked,"unfocused data cannot be motion");
    f.display_time+=16000000;f.focused=true;
    require(!history.update(f).hands[1].continuous,"focus recovery cannot bridge a missing sample");
    f.display_time+=16000000;f.result=XR_ERROR_RUNTIME_FAILURE;
    require(!history.update(f).hands[1].tracked,"partial action failure invalidates both paths");
    f.display_time+=16000000;f.result=XR_SUCCESS;
    require(!history.update(f).hands[1].continuous,"query recovery seeds history");
    f.display_time+=200000000;
    require(!history.update(f).hands[1].continuous,"long sample gaps do not create giant swings");
    f.display_time-=16000000;
    require(!history.update(f).hands[1].continuous,"reversed runtime time does not sweep backwards");
    f.display_time+=16000000;f.hands[1].grip.orientation={0,0,0,0};
    require(!history.update(f).hands[1].tracked,"invalid tracked quaternion is not published");
    history.reset();f.hands[1].grip.orientation.w=1;
    require(!history.update(f).hands[1].continuous,"explicit camera/weapon reset clears intervals");
    require(history.snapshot().display_time==f.display_time,"consumer reads the latest coherent publication");
    std::puts("Hand pose history passed with simulated poses; no game combat performed.");
}
