#pragma once
#include "modern_factory_clock.h"
#include "native_caller_context.h"
#include <cstring>

namespace w3vr::factory_clock::windows {
// Exact loaded code/PE identity and five runtime entries. No language handlers
// are called, and no unregistered frame or private object layout is guessed.
inline bool module_matches(const Profile* profile,std::uintptr_t module) noexcept {
    std::uintptr_t end{};
    if(!known(profile) || !offset(module,profile->image_bytes,end))return false;
    __try {
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x1000)return false;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(module+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
            nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->FileHeader.TimeDateStamp!=profile->timestamp ||
            nt->OptionalHeader.SizeOfImage!=profile->image_bytes)return false;
        for(const auto& pin:profile->pins){
            if(pin.rva>=profile->image_bytes || pin.bytes.size()>profile->image_bytes-pin.rva ||
                std::memcmp(reinterpret_cast<const void*>(module+pin.rva),pin.bytes.data(),pin.bytes.size()))return false;
        }
        for(std::size_t i=0;i<profile->unwind.size();++i){
            const auto& node=profile->unwind[i];DWORD64 base{};
            auto* entry=RtlLookupFunctionEntry(module+node.begin,&base,nullptr);
            if(!entry || base!=module || entry->BeginAddress!=node.begin || entry->EndAddress!=node.end ||
                entry->UnwindData!=node.data)return false;
            const auto* info=reinterpret_cast<const std::uint8_t*>(module+node.data);
            if(info[0]!=(1u | (node.flags<<3)) || info[2]!=node.slots || info[3]!=0)return false;
            if(i+1<profile->unwind.size()){
                RUNTIME_FUNCTION next{};std::memcpy(&next,info+4+((node.slots+1)&~1u)*2,sizeof(next));
                const auto& expected=profile->unwind[i+1];
                if(next.BeginAddress!=expected.begin || next.EndAddress!=expected.end || next.UnwindData!=expected.data)return false;
            }
        }
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool read_once(const Profile* profile,std::uintptr_t module,std::uintptr_t expected_owner,
    std::uintptr_t descriptor,Values& out) noexcept {
    std::uintptr_t end{},tick_address{},viewport_address{},expected_receiver{};
    if(!known(profile) || !offset(module,profile->image_bytes,end) || !expected_owner || (expected_owner&7) ||
        !descriptor || (descriptor&7) || !offset(expected_owner,0x140,tick_address) ||
        tick_address>UINTPTR_MAX-sizeof(std::int64_t) || !offset(expected_owner,0x100,viewport_address) ||
        !offset(expected_owner,0x58,expected_receiver))return false;
    __try {
        Values candidate{};
        std::memcpy(&candidate.owner,reinterpret_cast<const void*>(module+profile->clock_global),sizeof(candidate.owner));
        if(candidate.owner!=expected_owner)return false;
        std::memcpy(&candidate.table,reinterpret_cast<const void*>(candidate.owner),sizeof(candidate.table));
        if(!table_matches(profile,module,candidate.table))return false;
        std::memcpy(&candidate.camera.descriptor_viewport,reinterpret_cast<const void*>(descriptor),sizeof(std::uintptr_t));
        std::memcpy(&candidate.camera.owner_viewport,reinterpret_cast<const void*>(viewport_address),sizeof(std::uintptr_t));
        std::uintptr_t interface_address{};
        if(!candidate.camera.descriptor_viewport || (candidate.camera.descriptor_viewport&7) ||
            candidate.camera.descriptor_viewport!=candidate.camera.owner_viewport ||
            !offset(candidate.camera.descriptor_viewport,8,interface_address) || interface_address>UINTPTR_MAX-8)return false;
        std::memcpy(&candidate.camera.receiver,reinterpret_cast<const void*>(interface_address),sizeof(std::uintptr_t));
        if(candidate.camera.receiver!=expected_receiver)return false;
        std::memcpy(&candidate.camera.table,reinterpret_cast<const void*>(expected_receiver),sizeof(std::uintptr_t));
        // Verify the exact secondary table BEFORE reading its virtual getter.
        bool secondary_matches=false;
        for(std::size_t i=0;i<profile->primary_vtables.size();++i){
            if(candidate.table==module+profile->primary_vtables[i] &&
                candidate.camera.table==module+profile->camera_vtables[i])secondary_matches=true;
        }
        if(!secondary_matches)return false;
        std::memcpy(&candidate.camera.getter,reinterpret_cast<const void*>(candidate.camera.table+11*8),sizeof(std::uintptr_t));
        if(!camera_matches(profile,module,candidate))return false;
        std::memcpy(&candidate.ticks,reinterpret_cast<const void*>(tick_address),sizeof(candidate.ticks));
        std::memcpy(&candidate.scale,reinterpret_cast<const void*>(module+profile->scale_global),sizeof(candidate.scale));
        out=candidate;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool read_values(const Profile* profile,std::uintptr_t module,std::uintptr_t expected_owner,
    std::uintptr_t descriptor,Values& out) noexcept {
    Values candidate{},repeat{};
    if(!read_once(profile,module,expected_owner,descriptor,candidate) ||
        !read_once(profile,module,expected_owner,descriptor,repeat) || repeat.owner!=candidate.owner ||
        repeat.table!=candidate.table || repeat.ticks!=candidate.ticks || repeat.scale!=candidate.scale ||
        repeat.camera!=candidate.camera)return false;
    out=candidate;return true;
}
inline bool observe(const engine_camera::TemporalContract* contract,std::uintptr_t module,
    std::uintptr_t return_pc,std::uintptr_t descriptor,BorrowedScope& scope) noexcept {
    const auto* profile=selected(contract);std::uintptr_t expected_pc{},fragment{};
    if(!profile || !offset(module,profile->factory_return,expected_pc) || return_pc!=expected_pc ||
        !offset(module,profile->fragment,fragment) || !module_matches(profile,module)){scope.revoke();return false;}
    native_caller_context::Snapshot snapshot{};
    if(!native_caller_context::capture({return_pc,module,fragment,24},snapshot)){scope.revoke();return false;}
    const auto thread=GetCurrentThreadId();
    const Caller caller{snapshot.pc,snapshot.image_base,snapshot.unwind_entry,snapshot.sp,
        snapshot.rbp,snapshot.rdi,snapshot.stack_low,snapshot.stack_high,thread};
    // Reject a different stack argument BEFORE dereferencing a clock owner.
    if(!caller_matches(profile,caller,descriptor,thread)){scope.revoke();return false;}
    Values values{};
    if(!read_values(profile,module,caller.rdi,descriptor,values)){scope.revoke();return false;}
    return scope.begin(contract,profile,caller,descriptor,values,module_matches(profile,module),thread);
}
inline bool revalidate(const engine_camera::TemporalContract* contract,BorrowedScope& scope) noexcept {
    const auto* profile=scope.profile();const auto& caller=scope.borrowed_caller();Values values{};
    if(!profile || selected(contract)!=profile || GetCurrentThreadId()!=caller.thread ||
        !module_matches(profile,caller.module) || !read_values(profile,caller.module,caller.rdi,scope.borrowed_descriptor(),values)){
        scope.revoke();return false;
    }
    return scope.revalidate(contract,values,true,GetCurrentThreadId());
}
} // namespace w3vr::factory_clock::windows
