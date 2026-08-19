#pragma once
#include <Onyx/Types/TypeId.h>
#include <Onyx/Types/ITypeHandler.h>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>

namespace Onyx::Types {

/// Central registry for asset type handlers, keyed by TypeId.
/// Handlers self-register at static init via ONYX_REGISTER_FILE_TYPE (or, in
/// app-level code, via app-specific registration macros).
class TypeRegistry {
public:
    /// Meyer's singleton - safe against static init order fiasco.
    static TypeRegistry& Get();

    /// Register a handler by its TypeId (identified at the filesystem layer).
    void RegisterByTypeId(std::unique_ptr<ITypeHandler> handler);

    /// Direct lookup by TypeId.
    ///
    /// The TypeId -> handler index is built lazily on the first lookup, not at
    /// registration time. Handlers self-register during static init, but their
    /// GetId() reads app-owned TypeId handles that stay 0 until the app seeds
    /// the TypeCatalog inside main(). Indexing eagerly filed every handler
    /// under id 0, each overwriting the last, so only the final registration
    /// was ever reachable.
    ITypeHandler* Resolve(TypeId id) const;

    /// Drop the index so the next Resolve() rebuilds it. Registration does this
    /// automatically; call it directly only if a handler's id changes after the
    /// fact (e.g. the catalog is re-seeded between tests).
    void InvalidateIndex();

    /// Iterate all registered handlers (used by AssetVisibility / debug listing).
    const std::vector<ITypeHandler*>& AllHandlers() const { return m_allHandlers; }

private:
    TypeRegistry() = default;

    /// Rebuild m_idMap from m_allHandlers. Caller must hold m_mutex.
    void RebuildIndexLocked() const;

    mutable std::mutex m_mutex;

    // Owns all handlers
    std::vector<std::unique_ptr<ITypeHandler>> m_owned;
    // Non-owning index: all registered handlers
    std::vector<ITypeHandler*> m_allHandlers;
    // TypeId -> handler. Rebuilt lazily; guarded by m_mutex.
    mutable std::unordered_map<uint32_t, ITypeHandler*> m_idMap;
    mutable bool m_indexDirty = true;
};

} // namespace Onyx::Types
