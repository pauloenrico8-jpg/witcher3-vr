#include "modern_camera_defaults.h"
#include <cstdio>
#include <cstdlib>
namespace d=w3vr::camera_defaults;
using C=w3vr::engine_camera::TemporalContract;
constexpr const C* contract=&w3vr::engine_camera::remastered_1048522;
constexpr std::uintptr_t base=0x140000000;
unsigned checks{};
void require(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
struct Reader {
    d::Words values=d::identity_words;
    unsigned calls{},fail{},corrupt_pin{},matrix_reads{},change_on_second{};
    bool operator()(std::uintptr_t address,void* dst,std::size_t count){
        const auto call=++calls;
        if(call==fail){std::memset(dst,0xDD,count);return false;}
        for(const auto& pin:d::current.loads)if(address==base+pin.rva && count==pin.bytes.size()){
            std::memcpy(dst,pin.bytes.data(),count);if(call==corrupt_pin)static_cast<std::uint8_t*>(dst)[0]^=1;return true;
        }
        if(address==base+d::current.matrix && count==sizeof(values)){
            ++matrix_reads;auto copy=values;if(change_on_second && matrix_reads==2)copy[change_on_second-1]^=1;
            std::memcpy(dst,copy.data(),count);return true;
        }
        return false;
    }
};
void value_checks(){
    Reader reader;d::Words out{};out.fill(0xA5A5A5A5);
    require(d::read(contract,&d::current,base,reader,out) && out==d::identity_words && reader.calls==6,"exact two-pass default observations rejected");
    for(unsigned field=0;field<16;++field){Reader r;r.values[field]^=1;auto unchanged=out;
        require(!d::read(contract,&d::current,base,r,unchanged) && unchanged==out,"modified matrix word published output");}
    for(unsigned field=1;field<=16;++field){Reader r;r.change_on_second=field;auto unchanged=out;
        require(!d::read(contract,&d::current,base,r,unchanged) && unchanged==out && r.calls==6,"matrix changed between observations accepted");}
    for(unsigned fail=1;fail<=6;++fail){Reader r;r.fail=fail;auto unchanged=out;
        require(!d::read(contract,&d::current,base,r,unchanged) && unchanged==out && r.calls==fail,"partial protected read published bytes or continued reading");}
    for(unsigned pin:{1u,2u,4u,5u}){Reader r;r.corrupt_pin=pin;auto unchanged=out;
        require(!d::read(contract,&d::current,base,r,unchanged) && unchanged==out && r.calls==pin,"unknown reader instruction accepted");}
    auto copied=*contract;
    for(const C* wrong:std::array<const C*,4>{nullptr,&copied,&w3vr::engine_camera::legacy_404,&w3vr::engine_camera::remastered_500c}){
        Reader r;auto unchanged=out;require(!d::read(wrong,&d::current,base,r,unchanged) && r.calls==0 && unchanged==out,"wrong or copied contract read Game fields");}
    auto profile=d::current;
    for(const d::Profile* wrong:std::array<const d::Profile*,2>{nullptr,&profile}){Reader r;auto unchanged=out;
        require(!d::read(contract,wrong,base,r,unchanged) && r.calls==0 && unchanged==out,"null or copied profile gained admission");}
    for(std::uintptr_t module:std::array<std::uintptr_t,3>{0,base+8,UINTPTR_MAX-0xFFF}){Reader r;auto unchanged=out;
        require(!d::read(contract,&d::current,module,r,unchanged) && r.calls==0 && unchanged==out,"bad module address dereferenced");}
    // Output aliases reader storage; staging must preserve the successful value.
    Reader alias;require(d::read(contract,&d::current,base,alias,alias.values) && alias.values==d::identity_words,"owned output/input alias corrupted words");
}
#if defined(_MSC_VER) && defined(_WIN32)
void windows_checks(){
    constexpr std::size_t page=4096;
    auto* memory=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,d::current.image_bytes,MEM_RESERVE,PAGE_NOACCESS));
    require(memory!=nullptr,"could not reserve OWN test image");
    for(const auto offset:{d::current.loads[0].rva&~(page-1),d::current.matrix&~(page-1)})
        require(VirtualAlloc(memory+offset,page,MEM_COMMIT,PAGE_READWRITE)==memory+offset,"could not commit OWN test page");
    for(const auto& pin:d::current.loads)std::memcpy(memory+pin.rva,pin.bytes.data(),pin.bytes.size());
    std::memcpy(memory+d::current.matrix,d::identity_words.data(),sizeof(d::identity_words));
    const auto module=reinterpret_cast<std::uintptr_t>(memory);d::Words out{};
    require(d::read(contract,&d::current,module,&d::guarded_read,out) && out==d::identity_words,"shared guarded reader rejected OWN memory");
    const auto saved=out;memory[d::current.loads[1].rva]^=1;
    require(!d::read(contract,&d::current,module,&d::guarded_read,out) && out==saved,"protected reader accepted changed load instruction");
    memory[d::current.loads[1].rva]^=1;memory[d::current.matrix+40]^=1;
    require(!d::read(contract,&d::current,module,&d::guarded_read,out) && out==saved,"protected reader accepted changed matrix");
    memory[d::current.matrix+40]^=1;DWORD old{};
    auto* matrix_page=memory+(d::current.matrix&~(page-1));
    require(VirtualProtect(matrix_page,page,PAGE_NOACCESS,&old)!=FALSE,"could not protect OWN matrix page");
    require(!d::read(contract,&d::current,module,&d::guarded_read,out) && out==saved,"unreadable OWN page produced output");
    DWORD previous{};require(VirtualProtect(matrix_page,page,PAGE_READWRITE,&previous)!=FALSE,"could not restore OWN page protection");
    require(VirtualFree(matrix_page,page,MEM_DECOMMIT)!=FALSE,"could not decommit OWN matrix page");
    require(!d::read(contract,&d::current,module,&d::guarded_read,out) && out==saved,"uncommitted OWN page produced output");
    require(VirtualFree(memory,0,MEM_RELEASE)!=FALSE,"could not release OWN test reservation");
}
#endif
int main(){value_checks();
#if defined(_MSC_VER) && defined(_WIN32)
windows_checks();
#endif
std::printf("PASS default matrix observation: %u checks; exact values/load sites, atomic reads, changes, wrong profiles and OWN Windows memory protection.\n",checks);
std::puts("NATIVE_GAME=false HOST_CALLBACKS=false NATIVE_INITIALIZER=false EXTRA_BUILDER=false SDK_INIT=false GPU=false QUEST=false VR_FUNCTIONAL=false");}
