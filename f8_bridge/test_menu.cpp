#include "menu_state.h"
#include <cassert>
#include <iostream>
using namespace mafiafsr;
int main() {
    MenuState menu;
    BackendEvidence offline{};
    const auto entries = makeChoices(offline);
    for (int i=0;i<3;++i) assert(entries[static_cast<std::size_t>(i)].available);
    for (int i=3;i<7;++i) assert(!entries[static_cast<std::size_t>(i)].available);
    for (auto mode : {Mode::NativeAa,Mode::Quality,Mode::Balanced,Mode::Performance})
        assert(!menu.select(mode,offline).accepted);
    auto original = menu.select(Mode::OriginalDlaa,offline);
    assert(original.accepted && original.originalIndex == 2);
    BackendEvidence syntheticOnly{};
    syntheticOnly.providerIs412=true;
    syntheticOnly.dx11Dx12BridgeReady=true;
    assert(!menu.select(Mode::NativeAa,syntheticOnly).accepted);
    BackendEvidence inGame{true,true,true,true,true,false};
    auto aa = menu.select(Mode::NativeAa,inGame);
    assert(aa.accepted && aa.originalIndex == 1);
    assert(menu.displayedIndex(1,inGame)==3);
    assert(!menu.select(Mode::Quality,inGame).accepted);
    assert(menu.displayedIndex(1,offline)==1);
    inGame.lowerResolution3DVerified=true;
    for (auto mode:{Mode::Quality,Mode::Balanced,Mode::Performance}) {
        auto s=menu.select(mode,inGame);
        assert(s.accepted && s.originalIndex==1);
    }
    auto off=menu.select(Mode::OriginalOff,inGame);
    assert(off.accepted && off.originalIndex==0);
    std::cout<<"F8 menu policy tests passed\n";
}
