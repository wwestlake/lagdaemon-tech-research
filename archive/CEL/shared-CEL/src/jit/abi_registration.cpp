#include "abi_registration.h"

#include <unordered_map>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>

namespace ce::lang::jit {

llvm::Error RegisterAbiTrampolines(llvm::orc::LLJIT& lljit, const IntrinsicDomainSet& allowed,
                                    const std::vector<AbiSymbol>& extraSymbols,
                                    const std::unordered_map<std::string, IntrinsicDomain>& extraDomains) {
    std::unordered_map<std::string, IntrinsicDomain> domains = GetAbiSymbolDomains();
    domains.insert(extraDomains.begin(), extraDomains.end());

    std::vector<AbiSymbol> all = GetAbiTrampolines();
    all.insert(all.end(), extraSymbols.begin(), extraSymbols.end());

    llvm::orc::SymbolMap symbols;
    for (const AbiSymbol& sym : all) {
        // A symbol with no domain entry (only ce_watchdog_tick) is a
        // runtime safety mechanism, not a script capability -- always
        // registered. Everything else is filtered against `allowed`.
        const auto domainIt = domains.find(sym.name);
        if (domainIt != domains.end() && !allowed.Contains(domainIt->second)) {
            continue;
        }
        symbols[lljit.mangleAndIntern(sym.name)] = { llvm::orc::ExecutorAddr::fromPtr(sym.address),
                                                      llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable };
    }
    return lljit.getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols)));
}

} // namespace ce::lang::jit
