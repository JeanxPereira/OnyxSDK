#pragma once
#include <Onyx/Types/TypeId.h>
#include <Onyx/Types/ITypeHandler.h>
#include <unordered_map>
#include <memory>
#include <vector>
#include <cstdint>

namespace Onyx::Types {

/// Central registry for asset type handlers, keyed by TypeId.
/// Handlers self-register at static init via REGISTER_FILE_TYPE (or, in
/// app-level code, via app-specific registration macros).
class TypeRegistry {
public:
    /// Meyer's singleton — safe against static init order fiasco.
    static TypeRegistry& Get();

    /// Register a handler by its TypeId (identified at the filesystem layer).
    void RegisterByTypeId(std::unique_ptr<ITypeHandler> handler);

    /// Direct lookup by TypeId.
    ITypeHandler* Resolve(TypeId id) const;

    /// Iterate all registered handlers (used by AssetVisibility / debug listing).
    const std::vector<ITypeHandler*>& AllHandlers() const { return m_allHandlers; }

private:
    TypeRegistry() = default;

    // Owns all handlers
    std::vector<std::unique_ptr<ITypeHandler>> m_owned;
    // Non-owning index: all registered handlers
    std::vector<ITypeHandler*> m_allHandlers;
    // TypeId → handler
    std::unordered_map<uint32_t, ITypeHandler*> m_idMap;
};

} // namespace Onyx::Types
