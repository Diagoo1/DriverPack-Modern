// SPDX-License-Identifier: GPL-3.0-or-later
#include "origin_api.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include <stdexcept>

namespace sdiomodern {
Result decodeLegacyInfResult(int status,bool reboot){
    // The original SDIO helper uses 1 for success, unlike Win32 error codes.
    Result result;const bool success=status==1;result.outcome=success?Outcome::Success:Outcome::Failure;
    result.nativeError=success?0:static_cast<std::uint32_t>(status?status:31);
    result.rebootRequired=success && reboot;result.message=success?L"Windows completed the INF installation.":L"Windows rejected the INF installation.";return result;
}
std::wstring canonicalId(const std::wstring& value)
{
    std::wstring out=value;
    // Windows device instance IDs are case-insensitive. Do not match by a
    // substring or by the device display name (which is neither unique nor stable).
    std::transform(out.begin(),out.end(),out.begin(),[](wchar_t c){return std::towupper(c);});
    return out;
}
bool safeRelativeInfPath(const std::wstring& path)
{
    if(path.empty() || path.size()>2048) return false;
    if(path[0]==L'/' || path[0]==L'\\') return false;
    if(path.find_first_of(L":\"\r\n\t*?<>|")!=std::wstring::npos) return false;
    if(path.find(L'\0')!=std::wstring::npos) return false;
    std::wstring part;
    for(std::size_t i=0;i<=path.size();++i) {
        if(i==path.size() || path[i]==L'/' || path[i]==L'\\') {
            if(part==L".." || part==L"." || part.empty() || part.back()==L'.' || part.back()==L' ')return false;
            const std::wstring base=canonicalId(part.substr(0,part.find(L'.')));
            if(base==L"CON" || base==L"PRN" || base==L"AUX" || base==L"NUL" ||
               (base.size()==4 && (base.substr(0,3)==L"COM" || base.substr(0,3)==L"LPT") &&
                base[3]>=L'1' && base[3]<=L'9'))return false;
            part.clear();
        } else {
            if(path[i]<32)return false;
            part+=path[i];
        }
    }
    const std::wstring upper=canonicalId(path);
    return upper.size()>4 && upper.substr(upper.size()-4)==L".INF";
}
std::vector<CheckedSelection> validatePlan(const Snapshot& snapshot,const InstallPlan& plan)
{
    if(!snapshot.capabilities.nativeCore || !snapshot.capabilities.matching ||
       !snapshot.capabilities.localInstall)throw std::runtime_error("Native install capability is unavailable.");
    if(!plan.confirmed)throw std::runtime_error("Explicit installation confirmation is required.");
    if(snapshot.generation==0 || plan.generation!=snapshot.generation)
        throw std::runtime_error("The scan is stale. Rescan and review the installation plan.");
    if(plan.selections.empty())throw std::runtime_error("No drivers were selected.");
    if(plan.createRestorePoint && !snapshot.capabilities.restorePoint)
        throw std::runtime_error("Restore point creation is unavailable. No driver was changed.");
    std::set<std::wstring> seen;
    std::vector<CheckedSelection> out;
    for(const Selection& selection:plan.selections) {
        const std::wstring key=canonicalId(selection.deviceInstanceId);
        if(key.empty() || !seen.insert(key).second)
            throw std::runtime_error("A device is missing or selected more than once.");
        const Device* device=0;
        for(const Device& d:snapshot.devices)if(canonicalId(d.instanceId)==key) {
            if(device)throw std::runtime_error("Ambiguous device instance ID.");
            device=&d;
        }
        if(!device)throw std::runtime_error("Selected device no longer belongs to this scan.");
        const Candidate* candidate=0;
        for(const Candidate& c:device->candidates)if(c.token==selection.candidateToken && c.token!=0) {
            if(candidate)throw std::runtime_error("Ambiguous candidate token.");
            candidate=&c;
        }
        if(!candidate)throw std::runtime_error("Selected driver no longer belongs to this device.");
        if(!candidate->valid || (candidate->statusBits&0x008u))
            throw std::runtime_error("Origin marked this driver incompatible.");
        if(!candidate->local)throw std::runtime_error("The selected package is not locally available.");
        if(!candidate->recommended && !plan.allowAlternatives)
            throw std::runtime_error("Alternative drivers require a separate explicit confirmation.");
        if(!safeRelativeInfPath(candidate->inf))
            throw std::runtime_error("Unsafe or invalid relative INF path.");
        if(candidate->hardwareId.empty())throw std::runtime_error("The selected driver has no matching hardware ID.");
        // UpdateDriverForPlugAndPlayDevices targets a hardware ID, not a single
        // instance. Reject matches shared by different devices rather than
        // silently changing devices which were not selected in the UI.
        bool selectedDeviceMatches=false;
        for(const auto& id:device->hardwareIds)if(canonicalId(id)==canonicalId(candidate->hardwareId))selectedDeviceMatches=true;
        for(const auto& id:device->compatibleIds)if(canonicalId(id)==canonicalId(candidate->hardwareId))selectedDeviceMatches=true;
        if(!selectedDeviceMatches)throw std::runtime_error("Candidate hardware ID does not belong to the selected device.");
        int matchingDevices=0;
        const std::wstring match=canonicalId(candidate->hardwareId);
        for(const Device& other:snapshot.devices) {
            bool found=false;
            for(const auto& h:other.hardwareIds)if(canonicalId(h)==match)found=true;
            for(const auto& h:other.compatibleIds)if(canonicalId(h)==match)found=true;
            if(found)++matchingDevices;
        }
        if(matchingDevices!=1)throw std::runtime_error("Hardware ID is absent or shared by several devices; broad installation is blocked.");
        out.push_back(CheckedSelection{device,candidate});
    }
    return out;
}
}
