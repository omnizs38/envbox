#include "envbox/engine/adapter.hpp"

namespace envbox::engine {

void AdapterRegistry::add(std::shared_ptr<Adapter> adapter) {
    if (adapter) {
        adapters_.push_back(std::move(adapter));
    }
}

const Adapter* AdapterRegistry::find(std::string_view adapter_id) const {
    for (const auto& adapter : adapters_) {
        if (adapter->id() == adapter_id) {
            return adapter.get();
        }
    }
    return nullptr;
}

std::vector<const Adapter*> AdapterRegistry::all() const {
    std::vector<const Adapter*> out;
    out.reserve(adapters_.size());
    for (const auto& adapter : adapters_) {
        out.push_back(adapter.get());
    }
    return out;
}

}
