// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clicknova::accessibility::ipc {
namespace ca = clicknova::accessibility::candidates;
// Private, versioned, length-prefixed IPC. Only local same-executable children
// are launched. Neither Qt/QML nor network endpoints receive selector strings.
inline constexpr std::uint32_t maxFrame = 64 * 1024;
inline constexpr std::uint32_t requestMagic = 0x32514e43u;  // CNQ2 (little endian)
inline constexpr std::uint32_t responseMagic = 0x32524e43u; // CNR2
inline constexpr std::uint32_t protocolVersion = 2;
enum class Operation : std::uint32_t { Scan = 1, Verify = 2 };
// Provider provenance is checked by matching the request and response; it is never
// inferred from QML display labels. Java and MSAA are read-only and cannot
// fall back to UIA or perform Verify/bind.
enum class Provider : std::uint32_t { Uia = 0, Msaa = 1, Java = 2 };
inline constexpr bool supportedProvider(Provider p) noexcept {
    return p == Provider::Uia || p == Provider::Msaa || p == Provider::Java;
}
inline constexpr bool permitted(Provider p, Operation o) noexcept {
    return supportedProvider(p) &&
           (o == Operation::Scan || (o == Operation::Verify && p == Provider::Uia));
}

struct Request {
    Operation operation{Operation::Scan};
    Provider provider{Provider::Uia};
    std::uint64_t nonce{};
    ca::Target target{};
    ca::Limits limits{};
    ca::Candidate candidate{};
};
struct Reply {
    Operation operation{Operation::Scan};
    Provider provider{Provider::Uia};
    std::uint64_t nonce{};
    ca::Result scan{};
    ca::VerifyResult verification{};
};
namespace detail {
// Identifiers originate as UTF-16 from UIA. Reject malformed UTF-8 on IPC
// rather than silently persisting ill-formed bytes from a corrupt worker.
inline bool utf8(const std::string& s) noexcept {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        unsigned n = 0;
        std::uint32_t cp = 0;
        std::uint32_t minimum = 0;
        if ((c & 0xe0) == 0xc0) {n = 2; cp = c & 0x1fu; minimum = 0x80;}
        else if ((c & 0xf0) == 0xe0) {n = 3; cp = c & 0x0fu; minimum = 0x800;}
        else if ((c & 0xf8) == 0xf0) {n = 4; cp = c & 0x07u; minimum = 0x10000;}
        else return false;
        if (s.size()-i < n) return false;
        for (unsigned j=1; j<n; ++j) {
            const auto t = static_cast<unsigned char>(s[i+j]);
            if ((t & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (t & 0x3fu);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            (cp >= 0x80 && cp <= 0x9f)) return false;
        i += n;
    }
    return true;
}
struct Writer {
    std::vector<std::uint8_t> b;
    void u32(std::uint32_t v) { for(int i=0;i<4;++i) b.push_back(static_cast<std::uint8_t>(v >> (8*i))); }
    void u64(std::uint64_t v) { for(int i=0;i<8;++i) b.push_back(static_cast<std::uint8_t>(v >> (8*i))); }
    bool str(const std::string& s) {
        if (s.size()>384 || (!s.empty() && (!ca::safeIdentity(s) || !utf8(s)))) return false;
        u32(static_cast<std::uint32_t>(s.size())); b.insert(b.end(),s.begin(),s.end());return true;
    }
    bool candidate(const ca::Candidate& c) {
        if(c.controlType<0 || c.controlType>100000) return false;
        u32(static_cast<std::uint32_t>(c.controlType));u32(c.enabled?1:0);
        return str(c.automationId)&&str(c.className);
    }
};
struct Reader {
    std::span<const std::uint8_t> b; std::size_t i{};
    bool u32(std::uint32_t& out) {
        if(i>b.size() || b.size()-i<4) return false;
        out=0;for(int j=0;j<4;++j)out|=static_cast<std::uint32_t>(b[i++])<<(8*j);return true;
    }
    bool u64(std::uint64_t& out) {
        if(i>b.size() || b.size()-i<8)return false;
        out=0;for(int j=0;j<8;++j)out|=static_cast<std::uint64_t>(b[i++])<<(8*j);return true;
    }
    bool str(std::string& out) {
        std::uint32_t n{};
        if(!u32(n)||n>384||i>b.size()||b.size()-i<n)return false;
        out.assign(reinterpret_cast<const char*>(b.data()+i),n);i+=n;
        return out.empty() || (ca::safeIdentity(out) && utf8(out));
    }
    bool candidate(ca::Candidate& c) {
        std::uint32_t type{},enabled{};
        if(!u32(type)||type>100000||!u32(enabled)||enabled>1)return false;
        c.controlType=static_cast<int>(type);c.enabled=enabled!=0;
        return str(c.automationId)&&str(c.className);
    }
    bool done() const {return i==b.size();}
};
inline bool operation(std::uint32_t v){return v==1||v==2;}
inline bool provider(std::uint32_t v){return v<=2;}
inline bool status(std::uint32_t v){return v<=static_cast<std::uint32_t>(ca::Status::Failed);}
inline bool verifyStatus(std::uint32_t v){return v<=static_cast<std::uint32_t>(ca::VerifyStatus::Failed);}
}
// Encoded payload has no outer length word; encodeFrame adds the framing.
inline bool encode(const Request& r,std::vector<std::uint8_t>& out){
    if(!r.nonce || !r.target.nativeHandle || !r.target.processId ||
       !ca::validLimits(r.limits) || !permitted(r.provider, r.operation))return false;
    detail::Writer w;w.u32(requestMagic);w.u32(protocolVersion);
    w.u32(static_cast<std::uint32_t>(r.operation));
    w.u32(static_cast<std::uint32_t>(r.provider));w.u64(r.nonce);
    w.u64(static_cast<std::uint64_t>(r.target.nativeHandle));w.u32(r.target.processId);
    w.u32(static_cast<std::uint32_t>(r.limits.maxVisited));
    w.u32(static_cast<std::uint32_t>(r.limits.maxCandidates));
    w.u32(r.limits.maxDepth);w.u32(static_cast<std::uint32_t>(r.limits.maxElapsedMs));
    if(r.operation==Operation::Verify) {
        if(!ca::eligible(r.candidate) || !w.candidate(r.candidate))return false;
    } else if(r.operation!=Operation::Scan)return false;
    if (w.b.size() > maxFrame) return false;
    out = std::move(w.b);
    return true;
}
inline bool decode(std::span<const std::uint8_t> bytes,Request& out){
    if(bytes.empty()||bytes.size()>maxFrame)return false;
    detail::Reader r{bytes};std::uint32_t magic{},version{},op{},provider{},pid{},visited{},count{},depth{},elapsed{};
    std::uint64_t nonce{},handle{};
    if(!r.u32(magic)||magic!=requestMagic||!r.u32(version)||version!=protocolVersion||
       !r.u32(op)||!detail::operation(op)||
       !r.u32(provider)||!detail::provider(provider)||!r.u64(nonce)||!nonce||
       !r.u64(handle)||!handle||handle>std::numeric_limits<std::uintptr_t>::max()||
       !r.u32(pid)||!pid||!r.u32(visited)||!r.u32(count)||!r.u32(depth)||
       !r.u32(elapsed)||elapsed>static_cast<std::uint32_t>(std::numeric_limits<int>::max()))return false;
    Request value;value.operation=static_cast<Operation>(op);
    value.provider=static_cast<Provider>(provider);value.nonce=nonce;
    if (!permitted(value.provider, value.operation)) return false;
    value.target={static_cast<std::uintptr_t>(handle),pid};
    value.limits={visited,count,depth,static_cast<int>(elapsed)};
    if(!ca::validLimits(value.limits))return false;
    if(value.operation==Operation::Verify&&(!r.candidate(value.candidate)||!ca::eligible(value.candidate)))return false;
    if (!r.done()) return false;
    out = std::move(value);
    return true;
}
inline bool encode(const Reply& reply,std::vector<std::uint8_t>& out){
    if(!reply.nonce || !permitted(reply.provider, reply.operation))return false;
    detail::Writer w;w.u32(responseMagic);w.u32(protocolVersion);
    w.u32(static_cast<std::uint32_t>(reply.operation));
    w.u32(static_cast<std::uint32_t>(reply.provider));w.u64(reply.nonce);
    if(reply.operation==Operation::Scan){
        const auto& scan=reply.scan;
        if(scan.items.size()>32||scan.visited>512)return false;
        // MSAA and Java previews never send selectors, even for Complete results.
        if(reply.provider == Provider::Msaa || reply.provider == Provider::Java) {
            for(const auto& c:scan.items)
                if(!c.automationId.empty() || !c.className.empty()) return false;
        }
        const auto state=static_cast<std::uint32_t>(scan.status);
        if(!detail::status(state))return false;
        w.u32(state);w.u32(static_cast<std::uint32_t>(scan.visited));
        w.u32(scan.truncated?1:0);w.u32(static_cast<std::uint32_t>(scan.items.size()));
        for(const auto& c:scan.items)if(!w.candidate(c))return false;
    } else if(reply.operation==Operation::Verify){
        const auto status=static_cast<std::uint32_t>(reply.verification.status);
        if(!detail::verifyStatus(status))return false;
        w.u32(status);
        if(reply.verification.status==ca::VerifyStatus::Verified){
            if(!ca::eligible(reply.verification.match)||!w.candidate(reply.verification.match))return false;
        }
    } else return false;
    if (w.b.size() > maxFrame) return false;
    out = std::move(w.b);
    return true;
}
inline bool decode(std::span<const std::uint8_t> bytes,Reply& out){
    if(bytes.empty()||bytes.size()>maxFrame)return false;
    detail::Reader r{bytes};std::uint32_t magic{},version{},op{},provider{},status{};
    std::uint64_t nonce{};
    if(!r.u32(magic)||magic!=responseMagic||!r.u32(version)||version!=protocolVersion||
       !r.u32(op)||!detail::operation(op)||
       !r.u32(provider)||!detail::provider(provider)||!r.u64(nonce)||!nonce||!r.u32(status))return false;
    Reply value;value.operation=static_cast<Operation>(op);
    value.provider=static_cast<Provider>(provider);value.nonce=nonce;
    if (!permitted(value.provider, value.operation)) return false;
    if(value.operation==Operation::Scan){
        std::uint32_t visited{},truncated{},count{};
        if(!detail::status(status)||!r.u32(visited)||visited>512||
           !r.u32(truncated)||truncated>1||!r.u32(count)||count>32)return false;
        value.scan.status=static_cast<ca::Status>(status);value.scan.visited=visited;
        value.scan.truncated=truncated!=0;
        for(std::uint32_t i=0;i<count;++i){
            ca::Candidate candidate;
            if(!r.candidate(candidate))return false;
            value.scan.items.push_back(std::move(candidate));
        }
        if(value.scan.status!=ca::Status::Complete&&!value.scan.items.empty())return false;
        if(value.provider==Provider::Msaa || value.provider==Provider::Java) {
            for(const auto& c:value.scan.items)
                if(!c.automationId.empty() || !c.className.empty())return false;
        }
    }else{
        if(!detail::verifyStatus(status))return false;
        value.verification.status=static_cast<ca::VerifyStatus>(status);
        if(value.verification.status==ca::VerifyStatus::Verified){
            if(!r.candidate(value.verification.match)||!ca::eligible(value.verification.match))return false;
        }
    }
    if (!r.done()) return false;
    out = std::move(value);
    return true;
}
// Validate the decoded worker result against the *original* request. A valid
// frame and nonce alone do not prove that the worker respected the caller's
// limits or verified the same candidate. Never normalize these violations:
// normalizing would hide an inconsistent / compromised child response.
inline bool matchesRequest(const Request& request, const Reply& reply) noexcept {
    if (!request.nonce || reply.nonce != request.nonce ||
        request.operation != reply.operation || request.provider != reply.provider ||
        !permitted(request.provider, request.operation) ||
        !ca::validLimits(request.limits))
        return false;
    if (request.operation == Operation::Scan) {
        const auto& result = reply.scan;
        if (result.status == ca::Status::Idle || result.status == ca::Status::Searching ||
            result.visited > request.limits.maxVisited ||
            result.items.size() > request.limits.maxCandidates ||
            result.items.size() > result.visited)
            return false;
        if (result.status != ca::Status::Complete && !result.items.empty())
            return false;
        if (request.provider == Provider::Msaa || request.provider == Provider::Java) {
            for (const auto& c : result.items)
                if (!c.automationId.empty() || !c.className.empty() || ca::eligible(c))
                    return false;
        }
        return true;
    }
    if (request.operation == Operation::Verify) {
        if (!ca::eligible(request.candidate)) return false;
        if (reply.verification.status != ca::VerifyStatus::Verified)
            return true;
        return ca::eligible(reply.verification.match) &&
            ca::sameIdentity(request.candidate, reply.verification.match);
    }
    return false;
}
inline bool frame(std::span<const std::uint8_t> payload,std::vector<std::uint8_t>& out){
    if(payload.empty()||payload.size()>maxFrame)return false;
    detail::Writer w;w.u32(static_cast<std::uint32_t>(payload.size()));
    w.b.insert(w.b.end(),payload.begin(),payload.end());out=std::move(w.b);return true;
}
inline bool unframe(std::span<const std::uint8_t> bytes,std::vector<std::uint8_t>& payload){
    if(bytes.size()<4||bytes.size()>maxFrame+4)return false;
    detail::Reader r{bytes};std::uint32_t size{};
    if(!r.u32(size)||size==0||size>maxFrame||bytes.size()-4!=size)return false;
    payload.assign(bytes.begin()+4,bytes.end());return true;
}
} // namespace clicknova::accessibility::ipc
