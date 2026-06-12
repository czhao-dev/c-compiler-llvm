#include "codegen.h"

#if defined(MINIC_HAS_LLVM)
#include <llvm/Config/llvm-config.h>
#endif

namespace minic {

std::string codegenStatus() {
#if defined(MINIC_HAS_LLVM)
    return "codegen: LLVM " LLVM_VERSION_STRING " available";
#else
    return "codegen: LLVM not configured";
#endif
}

} // namespace minic
