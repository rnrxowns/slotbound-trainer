#include "luajit.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string_view>

namespace lj {
namespace {

constexpr size_t kChunk = 4u << 20; // 一次读 4 MB
constexpr size_t kOverlap = 64;     // 块之间重叠一点，跨块的字符串也能找到
constexpr size_t kNeighborWindow = 512;

// 只扫私有、已提交、可读写的内存：LuaJIT 的堆都在这种区域里
template <typename Fn>
void ForEachHeapChunk(const GameProcess& game, Fn&& fn)
{
    std::vector<uint8_t> buf;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0;
    while (VirtualQueryEx(game.Handle(), reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t end = base + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE) {
            for (uintptr_t at = base; at < end;) {
                const uintptr_t start = at > base + kOverlap ? at - kOverlap : base;
                const size_t size = static_cast<size_t>(std::min<uintptr_t>(end, at + kChunk) - start);
                buf.resize(size);
                if (game.ReadRaw(start, buf.data(), size))
                    fn(start, buf.data(), size);
                at += kChunk;
            }
        }
        if (end <= addr)
            break;
        addr = end;
    }
}

// 找所有内容等于 names[i] 的字符串对象，结果按 names 的顺序返回
std::vector<std::vector<uintptr_t>> FindStringObjects(const GameProcess& game, const std::vector<std::string>& names)
{
    std::vector<std::string> patterns;
    for (const auto& n : names)
        patterns.push_back(n + '\0'); // 字符数据后面跟 '\0'，避免匹配到更长名字的前缀
    std::vector<std::vector<uintptr_t>> dataHits(names.size());

    ForEachHeapChunk(game, [&](uintptr_t start, const uint8_t* data, size_t size) {
        const std::string_view hay(reinterpret_cast<const char*>(data), size);
        for (size_t i = 0; i < patterns.size(); ++i) {
            for (size_t pos = hay.find(patterns[i]); pos != std::string_view::npos; pos = hay.find(patterns[i], pos + 1)) {
                const uintptr_t at = start + pos;
                auto& v = dataHits[i];
                if (v.empty() || v.back() < at) // 重叠区可能重复命中
                    v.push_back(at);
            }
        }
    });

    // 核对对象头：前面 0x18 字节是 GCstr，类型字节 = 4、长度 = 名字长度
    std::vector<std::vector<uintptr_t>> objects(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        for (uintptr_t dataAddr : dataHits[i]) {
            const uintptr_t obj = dataAddr - kStrHeader;
            uint8_t header[kStrHeader];
            if (!game.ReadRaw(obj, header, sizeof(header)))
                continue;
            uint32_t len;
            std::memcpy(&len, header + 0x14, sizeof(len));
            if (header[9] == kStrGct && len == names[i].size())
                objects[i].push_back(obj);
        }
    }
    return objects;
}

// 找所有 8 字节对齐、内容等于 keys 之一的位置
std::vector<std::vector<uintptr_t>> FindQwords(const GameProcess& game, const std::vector<uint64_t>& keys)
{
    std::vector<std::vector<uintptr_t>> hits(keys.size());
    ForEachHeapChunk(game, [&](uintptr_t start, const uint8_t* data, size_t size) {
        const size_t skip = (8 - (start & 7)) & 7;
        for (size_t off = skip; off + 8 <= size; off += 8) {
            uint64_t v;
            std::memcpy(&v, data + off, sizeof(v));
            for (size_t i = 0; i < keys.size(); ++i) {
                if (v == keys[i]) {
                    const uintptr_t at = start + off;
                    if (hits[i].empty() || hits[i].back() < at)
                        hits[i].push_back(at);
                }
            }
        }
    });
    return hits;
}

// node 前后 window 个节点里有没有 key == neighborKey 的（同一个节点数组按 0x18 对齐）
bool HasNeighbor(const GameProcess& game, uintptr_t node, uint64_t neighborKey, size_t window)
{
    const uintptr_t from = node - window * kNodeSize;
    std::vector<uint8_t> buf((2 * window + 1) * kNodeSize);
    if (!game.ReadRaw(from, buf.data(), buf.size())) {
        // 窗口跨到没提交的页了：退化成逐个节点读
        for (size_t i = 0; i <= 2 * window; ++i) {
            if (game.Read<uint64_t>(from + i * kNodeSize + 8) == neighborKey)
                return true;
        }
        return false;
    }
    for (size_t i = 0; i <= 2 * window; ++i) {
        uint64_t key;
        std::memcpy(&key, buf.data() + i * kNodeSize + 8, sizeof(key));
        if (key == neighborKey)
            return true;
    }
    return false;
}

void Logf(std::string& log, const char* fmt, ...)
{
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    log += line;
    log += '\n';
}

} // namespace

bool StillValid(const GameProcess& game, const Field& field)
{
    uint64_t slot[2];
    return field.Valid() && game.ReadRaw(field.node, slot, sizeof(slot)) && slot[1] == field.key && IsNumber(slot[0]);
}

bool ReadNumber(const GameProcess& game, const Field& field, double& out)
{
    uint64_t slot[2];
    if (!field.Valid() || !game.ReadRaw(field.node, slot, sizeof(slot)) || slot[1] != field.key || !IsNumber(slot[0]))
        return false;
    out = AsDouble(slot[0]);
    return true;
}

bool WriteNumber(const GameProcess& game, const Field& field, double value)
{
    // 写之前再核对一次 key：节点被搬走后原地址可能已经是别的数据
    return StillValid(game, field) && game.Write(field.node, FromDouble(value));
}

bool LocateGlobalNumber(const GameProcess& game, const std::string& name, const std::string& neighbor, Field& out,
    std::string& log)
{
    out = Field{};
    const bool useNeighbor = !neighbor.empty();
    std::vector<std::string> names{ name };
    if (useNeighbor)
        names.push_back(neighbor);

    const auto strings = FindStringObjects(game, names);
    Logf(log, "문자열 \"%s\": 객체 %zu개", name.c_str(), strings[0].size());
    if (useNeighbor)
        Logf(log, "문자열 \"%s\": 객체 %zu개", neighbor.c_str(), strings[1].size());
    if (strings[0].empty()) {
        Logf(log, "변수 이름을 찾지 못했습니다. 메인 메뉴나 로딩 중에는 변수가 없을 수 있습니다.");
        return false;
    }
    if (useNeighbor && strings[1].empty())
        Logf(log, "인접 변수 \"%s\"의 문자열 객체를 찾지 못했습니다.", neighbor.c_str());

    // 每个字符串对象对应一个带标签的 key；LuaJIT 字符串驻留，正常各只有一个
    std::vector<uint64_t> keys;
    for (uintptr_t s : strings[0])
        keys.push_back(kTagStr | s);
    const size_t nameKeys = keys.size();
    if (useNeighbor)
        for (uintptr_t s : strings[1])
            keys.push_back(kTagStr | s);

    const auto refs = FindQwords(game, keys);

    struct Candidate {
        uintptr_t node;
        uint64_t key;
        double value;
        bool inGlobals;
    };
    std::vector<Candidate> candidates;
    for (size_t k = 0; k < nameKeys; ++k) {
        for (uintptr_t keySlot : refs[k]) {
            const uintptr_t node = keySlot - 8;
            double value;
            if (!ReadNumber(game, { node, keys[k] }, value))
                continue;
            bool inGlobals = false;
            for (size_t n = nameKeys; n < keys.size() && !inGlobals; ++n)
                inGlobals = HasNeighbor(game, node, keys[n], kNeighborWindow);
            candidates.push_back({ node, keys[k], value, inGlobals });
            // 同名字段可能上千个（别的表、旧快照），只列带邻居的和前几个
            if (inGlobals || candidates.size() <= 5)
                Logf(log, "  후보 노드 0x%llX = %.0f%s", static_cast<unsigned long long>(node), value,
                    inGlobals ? " (인접 변수 확인)" : "");
        }
    }
    if (candidates.empty()) {
        Logf(log, "숫자 값을 가진 필드가 없습니다: \"%s\"", name.c_str());
        return false;
    }

    const Candidate* pick = nullptr;
    size_t inGlobalsCount = 0;
    for (const auto& c : candidates)
        if (c.inGlobals && ++inGlobalsCount == 1)
            pick = &c;
    if (candidates.size() == 1)
        pick = &candidates[0]; // 只有一个就是它，不管有没有邻居
    else if (inGlobalsCount != 1)
        pick = nullptr;        // 多个候选时要求带邻居的唯一
    if (useNeighbor)
        Logf(log, "전체 후보 %zu개. 앞뒤 %zu개 노드 안에서 \"%s\" 확인: %zu개.",
            candidates.size(), kNeighborWindow, neighbor.c_str(), inGlobalsCount);
    if (!pick) {
        Logf(log, "_G 필드 하나를 특정할 수 없습니다.");
        return false;
    }

    out.node = pick->node;
    out.key = pick->key;
    Logf(log, "%s 찾음: 노드 0x%llX, 현재 값 %.0f", name.c_str(), static_cast<unsigned long long>(out.node), pick->value);
    return true;
}

} // namespace lj
